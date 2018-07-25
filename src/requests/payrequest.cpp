#include "payrequest.h"
#include "requestdefines.h"
#include "requesttools.h"
#include "requests/broadcast.h"
#include "requests/multicast.h"
#include "requests/salestatusrequest.h"
#include "requests/authorizertatxrequest.h"
#include "rta/supernode.h"
#include "rta/fullsupernodelist.h"
#include "inout.h"
#include "jsonrpc.h"

#include <misc_log_ex.h>

namespace graft {


enum class PayHandlerState : int
{
    ClientRequest = 0,
    TxAuthReply,
    StatusReply
};


Status handleClientPayRequest(const Router::vars_t& vars, const graft::Input& input,
                        graft::Context& ctx, graft::Output& output)
{
    PayRequestJsonRpc req;

    if (!input.get(req)) {
        return errorInvalidParams(output);
    }
    const PayRequest &in = req.params;

    SupernodePtr supernode = ctx.global.get(CONTEXT_KEY_SUPERNODE, SupernodePtr());
    FullSupernodeListPtr fsl = ctx.global.get(CONTEXT_KEY_FULLSUPERNODELIST, FullSupernodeListPtr());
    // we don't really need to check address here, as we supposed to receive transaction
    if (!supernode->validateAddress(in.Address, supernode->testnet())) {
        return errorInvalidAddress(output);
    }

    int current_status = ctx.global.get(in.PaymentID + CONTEXT_KEY_STATUS, static_cast<int>(RTAStatus::None));
    if (errorFinishedPayment(current_status, output)) {
        return Status::Error;
    }

    std::vector<SupernodePtr> authSample;
    if (!fsl->buildAuthSample(in.BlockNumber, authSample) || authSample.size() != FullSupernodeList::AUTH_SAMPLE_SIZE) {
        return errorBuildAuthSample(output);
    }

    // TODO: send multicast to /cryptonode/authorize_rta_tx_request
    MulticastRequestJsonRpc cryptonode_req;
    for (const auto & sn : authSample) {
        cryptonode_req.params.receiver_addresses.push_back(sn->walletAddress());
    }

    Output innerOut;
    AuthorizeRtaTxRequest authTxReq;
    authTxReq.tx_blob = in.tx_blob;
    innerOut.loadT<serializer::JSON_B64>(authTxReq);
    cryptonode_req.method = "multicast";
    cryptonode_req.params.callback_uri =  "/cryptonode/authorize_rta_tx_request"; // "/method" appended on cryptonode side
    cryptonode_req.params.data = innerOut.data();
    // store payment id as we need it to change the sale/pay state in next call
    ctx.local["payment_id"] = in.PaymentID;
    // TODO: what is the purpose of PayData?
    PayData data(in.Address, in.BlockNumber, in.Amount);
    ctx.global[in.PaymentID + CONTEXT_KEY_PAY] = data;
    ctx.global[in.PaymentID + CONTEXT_KEY_STATUS] = static_cast<int>(RTAStatus::InProgress);

    output.load(cryptonode_req);
    output.uri = ctx.global.getConfig()->cryptonode_rpc_address + "/json_rpc/rta";
    LOG_PRINT_L0("calling cryptonode: " << output.uri);
    LOG_PRINT_L0("\t with data: " << output.data());
    return Status::Forward;
}

Status handleTxAuthReply(const Router::vars_t& vars, const graft::Input& input,
                                graft::Context& ctx, graft::Output& output)
{
    // check cryptonode reply
    MulticastResponseFromCryptonodeJsonRpc resp;
    std::string payment_id = ctx.local["payment_id"];

    JsonRpcErrorResponse error;
    if (!input.get(resp) || resp.error.code != 0 || resp.result.status != STATUS_OK) {

        ctx.global.remove(payment_id + CONTEXT_KEY_PAY);
        ctx.global.remove(payment_id + CONTEXT_KEY_STATUS);

        error.error.code = ERROR_INTERNAL_ERROR;
        error.error.message = "Error multicasting request";
        output.load(error);

        return Status::Error;
    }

    SupernodePtr supernode = ctx.global.get(CONTEXT_KEY_SUPERNODE, SupernodePtr());

    UpdateSaleStatusBroadcast ussb;
    ussb.address = supernode->walletAddress();
    ussb.Status =  ctx.global.get(payment_id + CONTEXT_KEY_STATUS, static_cast<int>((RTAStatus::InProgress)));
    ussb.PaymentID = payment_id;

    // sign message
    std::string msg = payment_id + ":" + to_string(ussb.Status);
    crypto::signature sign;
    supernode->signMessage(msg, sign);
    ussb.signature = epee::string_tools::pod_to_hex(sign);

    Output innerOut;
    innerOut.loadT<serializer::JSON_B64>(ussb);

    // send payload
    BroadcastRequestJsonRpc cryptonode_req;
    cryptonode_req.method = "broadcast";
    cryptonode_req.params.callback_uri = "/cryptonode/update_sale_status";
    cryptonode_req.params.data = innerOut.data();
    output.load(cryptonode_req);
    output.uri = ctx.global.getConfig()->cryptonode_rpc_address + "/json_rpc/rta";
    output.load(cryptonode_req);
    LOG_PRINT_L0("calling cryptonode: " << output.uri);
    LOG_PRINT_L0("\t with data: " << output.data());

    return Status::Forward;
}

Status handleStatusBroadcastReply(const Router::vars_t& vars, const graft::Input& input,
                                graft::Context& ctx, graft::Output& output)
{

    // TODO: check if cryptonode broadcasted status
    BroadcastResponseFromCryptonodeJsonRpc resp;
    JsonRpcErrorResponse error;
    if (!input.get(resp) || resp.error.code != 0 || resp.result.status != STATUS_OK) {
        error.error.code = ERROR_INTERNAL_ERROR;
        error.error.message = "Error broadcasting request";
        output.load(error);
        return Status::Error;
    }

    // prepare reply to the client
    string payment_id = ctx.local["payment_id"];
    PayResponseJsonRpc out;
    out.result.Result = STATUS_OK;
    output.load(out);
    return Status::Ok;
}


/*!
 * \brief payClientHandler - handles "/dapi/v2.0/pay" request
 * \param vars
 * \param input
 * \param ctx
 * \param output
 * \return
 */
Status payClientHandler(const Router::vars_t& vars, const graft::Input& input,
                        graft::Context& ctx, graft::Output& output)
{
    PayHandlerState state = ctx.local.hasKey(__FUNCTION__) ? ctx.local[__FUNCTION__] : PayHandlerState::ClientRequest;

    // state machine to perform two calls to cryptonode and return result to the client
    switch (state) {
    // client requested "/sale"
    case PayHandlerState::ClientRequest:
        LOG_PRINT_L0("called by client, payload: " << input.data());
        ctx.local[__FUNCTION__] = PayHandlerState::TxAuthReply;
        // call cryptonode's "/rta/multicast" to send sale data to auth sample
        // "handleClientPayRequest" returns Forward;
        return handleClientPayRequest(vars, input, ctx, output);
    case PayHandlerState::TxAuthReply:
        // handle "multicast" response from cryptonode, check it's status, send
        // "sale status" with broadcast to cryptonode
        LOG_PRINT_L0("SaleMulticast response from cryptonode: " << input.data());
        LOG_PRINT_L0("status: " << (int)ctx.local.getLastStatus());
        ctx.local[__FUNCTION__] = PayHandlerState::StatusReply;
        // handleSameMulticast returns Forward, call performed according traffic capture but after that moment
        // this handler never called again, but it supposed to be "broadcast" reply from cryptonode
        return handleTxAuthReply(vars, input, ctx, output);
    case PayHandlerState::StatusReply:
        // this code never reached and previous output (with "broadcast" request to cryptonode) returned to client
        LOG_PRINT_L0("SaleStatusBroadcast response from cryptonode: " << input.data());
        LOG_PRINT_L0("status: " << (int)ctx.local.getLastStatus());
        return handleStatusBroadcastReply(vars, input, ctx, output);
     default:
        LOG_ERROR("Internal error: unhandled state");
        abort();
    };


}

void registerPayRequest(Router &router)
{
    Router::Handler3 h1(nullptr, payClientHandler, nullptr);
    router.addRoute("/pay", METHOD_POST, h1);
}

}
