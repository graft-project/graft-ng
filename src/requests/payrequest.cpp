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
#include <cryptonote_protocol/blobdatatype.h>
#include <cryptonote_basic/cryptonote_format_utils.h>

#undef MONERO_DEFAULT_LOG_CATEGORY
#define MONERO_DEFAULT_LOG_CATEGORY "supernode.payrequest"

namespace graft {


enum class PayHandlerState : int
{
    ClientRequest = 0,
    TxAuthReply,
    StatusReply
};

// processes /dapi/.../pay
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

    if (in.Transactions.empty()) {
        return errorInvalidTransaction("tx empty", output);
    }
    // TODO: !implement tx vector in every interface!
    string tx_hex = in.Transactions[0];
    // parse tx and validate tx, read tx id
    cryptonote::transaction tx;
    crypto::hash tx_hash, tx_prefix_hash;
    cryptonote::blobdata tx_blob;

    if (!epee::string_tools::parse_hexstr_to_binbuff(tx_hex, tx_blob)) {
        return errorInvalidTransaction(tx_hex, output);
    }
    if (!cryptonote::parse_and_validate_tx_from_blob(tx_blob, tx, tx_hash, tx_prefix_hash)) {
        return errorInvalidTransaction(tx_hex, output);
    }

    std::vector<SupernodePtr> authSample;
    if (!fsl->buildAuthSample(in.BlockNumber, authSample) || authSample.size() != FullSupernodeList::AUTH_SAMPLE_SIZE) {
        return errorBuildAuthSample(output);
    }
    LOG_PRINT_L0(__FUNCTION__ << " incoming pay, tx: " << epee::string_tools::pod_to_hex(tx_hash) << ", payment_id: " << in.PaymentID);
    // map tx_id -> payment id
    ctx.global.set(epee::string_tools::pod_to_hex(tx_hash) + CONTEXT_KEY_PAYMENT_ID_BY_TXID,
                   in.PaymentID, RTA_TX_TTL);

    // send multicast to /cryptonode/authorize_rta_tx_request
    MulticastRequestJsonRpc cryptonode_req;
    for (const auto & sn : authSample) {
        cryptonode_req.params.receiver_addresses.push_back(sn->walletAddress());
    }

    Output innerOut;
    AuthorizeRtaTxRequest authTxReq;

    authTxReq.tx_hex = tx_hex;
    authTxReq.payment_id = in.PaymentID;
    authTxReq.amount = in.Amount;

    innerOut.loadT<serializer::JSON_B64>(authTxReq);
    cryptonode_req.method = "multicast";
    cryptonode_req.params.callback_uri =  "/cryptonode/authorize_rta_tx_request";
    cryptonode_req.params.data = innerOut.data();
    cryptonode_req.params.sender_address = supernode->walletAddress();
    // store payment id as we need it to change the sale/pay state in next call
    ctx.local["payment_id"] = in.PaymentID;
    // TODO: what is the purpose of PayData?
    PayData data(in.Address, in.BlockNumber, in.Amount);
    ctx.global[in.PaymentID + CONTEXT_KEY_PAY] = data;
    ctx.global[in.PaymentID + CONTEXT_KEY_STATUS] = static_cast<int>(RTAStatus::InProgress);

    output.load(cryptonode_req);
    output.path = "/json_rpc/rta";
    LOG_PRINT_L0(__FUNCTION__ << " calling cryptonode: " << output.path);
    LOG_PRINT_L0(__FUNCTION__  << "\t with data: " << output.data());
    return Status::Forward;
}

// handles response from cryptonode/rta/multicast call with tx auth request
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

    int status = ctx.global.get(payment_id + CONTEXT_KEY_STATUS, static_cast<int>((RTAStatus::InProgress)));
    buildBroadcastSaleStatusOutput(payment_id, status, supernode, output);


    LOG_PRINT_L0("calling cryptonode: " << output.path);
    LOG_PRINT_L0("\t with data: " << output.data());

    return Status::Forward;
}

// handles status broadcast resply - responses to the client;
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
    LOG_PRINT_L0("pay: response to client: " << output.data());
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
    // client requested "/pay"
    case PayHandlerState::ClientRequest:
        LOG_PRINT_L0("called by client, payload: " << input.data());
        ctx.local[__FUNCTION__] = PayHandlerState::TxAuthReply;
        // call cryptonode's "/rta/multicast" to send sale data to auth sample
        // "handleClientPayRequest" returns Forward;
        return handleClientPayRequest(vars, input, ctx, output);
    case PayHandlerState::TxAuthReply:
        // handle "multicast" response from cryptonode, check it's status, send
        // "sale status" with broadcast to cryptonode
        LOG_PRINT_L0("authorize_rta_tx_request multicast response from cryptonode: " << input.data());
        LOG_PRINT_L0("status: " << (int)ctx.local.getLastStatus());
        ctx.local[__FUNCTION__] = PayHandlerState::StatusReply;
        // handleSameMulticast returns Forward, call performed according traffic capture but after that moment
        // this handler never called again, but it supposed to be "broadcast" reply from cryptonode
        return handleTxAuthReply(vars, input, ctx, output);
    case PayHandlerState::StatusReply:
        LOG_PRINT_L0("sale status broadcast response from cryptonode: " << input.data());
        LOG_PRINT_L0("status: " << (int)ctx.local.getLastStatus());
        return handleStatusBroadcastReply(vars, input, ctx, output);
     default:
        LOG_ERROR("Internal error: unhandled state");
        abort();
    };


}

void registerPayRequest(Router &router)
{
    Router::Handler3 clientHandler(nullptr, payClientHandler, nullptr);
    router.addRoute("/pay", METHOD_POST, clientHandler);
}

}
