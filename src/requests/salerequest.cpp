#include "salerequest.h"
#include "requestdefines.h"
#include "requesttools.h"
#include "rta/supernode.h"
#include "rta/fullsupernodelist.h"
#include "requests/multicast.h"
#include "requests/broadcast.h"
#include "requests/salestatusrequest.h"

#include <string>

#undef MONERO_DEFAULT_LOG_CATEGORY
#define MONERO_DEFAULT_LOG_CATEGORY "supernode.salerequest"

BOOST_HANA_ADAPT_STRUCT(graft::SaleData,
                        Address,
                        BlockNumber,
                        Amount
                        );

namespace graft {

static const std::chrono::seconds SALE_TTL = std::chrono::seconds(60);

// message to be multicasted to auth sample
GRAFT_DEFINE_IO_STRUCT(SaleDataMulticast,
                       (SaleData, sale_data),
                       (std::string, paymentId),
                       (int, status),
                       (string, details)
                       );




enum class SaleHandlerState : int
{
    ClientRequest = 0,
    SaleMulticastReply,
    SaleStatusReply,
};


Status handleClientSaleRequest(const Router::vars_t& vars, const graft::Input& input, graft::Context& ctx, graft::Output& output)
{
    SaleRequestJsonRpc req;
    JsonRpcError error;
    error.code = 0;
    MDEBUG(__FUNCTION__ << " begin");

    if (!input.get(req)) {
        return errorInvalidParams(output);
    }

    const SaleRequest &in = req.params;

    if (in.Amount <= 0)
    {
        return errorInvalidAmount(output);
    }

    bool testnet = ctx.global["testnet"];
    if (!Supernode::validateAddress(in.Address, testnet))
    {
        return errorInvalidAddress(output);
    }

    std::string payment_id = in.PaymentID;
    if (payment_id.empty()) // request comes from POS.
    {
        payment_id = generatePaymentID();
    }


    SupernodePtr supernode = ctx.global.get(CONTEXT_KEY_SUPERNODE, SupernodePtr());
    FullSupernodeListPtr fsl = ctx.global.get(CONTEXT_KEY_FULLSUPERNODELIST, FullSupernodeListPtr());

    // reply to caller (POS)
    SaleData data(in.Address, supernode->daemonHeight(), in.Amount);

    // what needs to be multicasted to auth sample ?
    // 1. payment_id
    // 2. SaleData
    if (!in.SaleDetails.empty())
    {
        ctx.global.set(payment_id + CONTEXT_KEY_SALE_DETAILS, in.SaleDetails, SALE_TTL);
    }

    // generate auth sample
    std::vector<SupernodePtr> authSample;
    if (!fsl->buildAuthSample(data.BlockNumber, authSample) || authSample.size() != FullSupernodeList::AUTH_SAMPLE_SIZE) {
        return errorCustomError(MESSAGE_RTA_CANT_BUILD_AUTH_SAMPLE, ERROR_INVALID_PARAMS, output);
    }

    // here we need to perform two actions:
    // 1. multicast sale over auth sample
    // 2. broadcast sale status
    ctx.global.set(payment_id + CONTEXT_KEY_SALE, data, SALE_TTL);
    ctx.global.set(payment_id + CONTEXT_KEY_STATUS, static_cast<int>(RTAStatus::Waiting), SALE_TTL);

    // store SaleData, payment_id and status in local context, so when we got reply from cryptonode, we just pass it to client
    ctx.local["sale_data"]  = data;
    ctx.local["payment_id"]  = payment_id;

    // prepare output for multicast
    // multicast sale to the auth sample nodes
    SaleDataMulticast sdm;
    sdm.paymentId = payment_id;
    sdm.sale_data = data;
    sdm.status = static_cast<int>(RTAStatus::Waiting);
    sdm.details = in.SaleDetails;
    Output innerOut;
    innerOut.loadT<serializer::JSON_B64>(sdm);

    MulticastRequestJsonRpc cryptonode_req;

    for (const auto & sn : authSample) {
        cryptonode_req.params.receiver_addresses.push_back(sn->walletAddress());
    }
    MDEBUG(__FUNCTION__ << "processed client request, multicasting sale data for payment_id: " << payment_id);
    cryptonode_req.method = "multicast";
    cryptonode_req.params.callback_uri =  "/cryptonode/sale"; // "/method" appended on cryptonode side
    cryptonode_req.params.data = innerOut.data();
    output.load(cryptonode_req);
    output.path = "/json_rpc/rta";
    LOG_PRINT_L0("calling cryptonode: " << output.path);
    LOG_PRINT_L0("\t with data: " << output.data());
    MDEBUG(__FUNCTION__ << " end");

    return Status::Forward;
}


Status handleSaleMulticastReply(const Router::vars_t& vars, const graft::Input& input,
                                graft::Context& ctx, graft::Output& output)
{
    // check cryptonode reply
    MDEBUG(__FUNCTION__ << " begin");
    MulticastResponseFromCryptonodeJsonRpc resp;

    JsonRpcErrorResponse error;
    if (!input.get(resp) || resp.error.code != 0 || resp.result.status != STATUS_OK) {
        return errorCustomError("Error multicasting request", ERROR_INTERNAL_ERROR, output);
    }

    SupernodePtr supernode = ctx.global.get(CONTEXT_KEY_SUPERNODE, SupernodePtr());

    string payment_id = ctx.local["payment_id"];
    int status = ctx.global.get(payment_id + CONTEXT_KEY_STATUS, static_cast<int>((RTAStatus::Waiting)));

    buildBroadcastSaleStatusOutput(payment_id, status, supernode, output);
    MDEBUG(__FUNCTION__ << "processed sale multicast, broadcasting sale status for payment_id: " << payment_id);
    LOG_PRINT_L0("calling cryptonode: " << output.path);
    LOG_PRINT_L0("\t with data: " << output.data());
    MDEBUG(__FUNCTION__ << " end");

    return Status::Forward;
}

Status handleSaleStatusBroadcastReply(const Router::vars_t& vars, const graft::Input& input,
                                graft::Context& ctx, graft::Output& output)
{

    // TODO: check if cryptonode broadcasted status
    MDEBUG(__FUNCTION__ << " begin");
    BroadcastResponseFromCryptonodeJsonRpc resp;
    JsonRpcErrorResponse error;
    if (!input.get(resp) || resp.error.code != 0 || resp.result.status != STATUS_OK) {
        return errorCustomError("Error broadcasting request", ERROR_INTERNAL_ERROR, output);
    }

    // prepare reply to the client
    SaleData data = ctx.local["sale_data"];
    string payment_id = ctx.local["payment_id"];
    SaleResponseJsonRpc out;
    out.result.BlockNumber = data.BlockNumber;
    out.result.PaymentID = payment_id;
    output.load(out);
    MDEBUG(__FUNCTION__ << "processed sale status broadcast, reply to client, payment_id: " << payment_id);
    MDEBUG(__FUNCTION__ << " end");
    return Status::Ok;
}

/*!
 * \brief saleClientHandler - handles /dapi/v2.0/sale POS request
 * \param vars
 * \param input
 * \param ctx
 * \param output
 * \return
 */
Status saleClientHandler(const Router::vars_t& vars, const graft::Input& input,
                         graft::Context& ctx, graft::Output& output)
{

    SaleHandlerState state = ctx.local.hasKey(__FUNCTION__) ? ctx.local[__FUNCTION__] : SaleHandlerState::ClientRequest;
    MDEBUG(__FUNCTION__ << ", state: " << int(state));
    // state machine to perform two calls to cryptonode and return result to the client
    switch (state) {
    // client requested "/sale"
    case SaleHandlerState::ClientRequest:
        LOG_PRINT_L0("called by client, payload: " << input.data());
        ctx.local[__FUNCTION__] = SaleHandlerState::SaleMulticastReply;
        // call cryptonode's "/rta/multicast" to send sale data to auth sample
        // "handleClientSaleRequest" returns Forward;
        return handleClientSaleRequest(vars, input, ctx, output);
    case SaleHandlerState::SaleMulticastReply:
        // handle "multicast" response from cryptonode, check it's status, send
        // "sale status" with broadcast to cryptonode
        LOG_PRINT_L0("SaleMulticast response from cryptonode: " << input.data());
        LOG_PRINT_L0("status: " << (int)ctx.local.getLastStatus());
        ctx.local[__FUNCTION__] = SaleHandlerState::SaleStatusReply;
        return handleSaleMulticastReply(vars, input, ctx, output);
    case SaleHandlerState::SaleStatusReply:
        LOG_PRINT_L0("SaleStatusBroadcast response from cryptonode: " << input.data());
        LOG_PRINT_L0("status: " << (int)ctx.local.getLastStatus());
        return handleSaleStatusBroadcastReply(vars, input, ctx, output);
     default:
        LOG_ERROR("Internal error: unhandled state");
        abort();
    };

}

/*!
 * \brief saleCryptonodeHandler - handles /dapi/v2.0/cryptonode/sale - call coming from cryptonode (multicasted), we just need to store "sale_data"
 * \param vars
 * \param input
 * \param ctx
 * \param output
 * \return
 */
Status saleCryptonodeHandler(const Router::vars_t& vars, const graft::Input& input,
                             graft::Context& ctx, graft::Output& output)
{

    MulticastRequestJsonRpc req;
    if (!input.get(req)) {
        return errorInvalidParams(output);
    }

    JsonRpcError error;
    error.code = 0;

    SaleDataMulticast sdm;

    graft::Input innerInput;
    innerInput.load(req.params.data);

    LOG_PRINT_L0("input loaded");
    if (!innerInput.getT<serializer::JSON_B64>(sdm)) {
        return errorInvalidParams(output);
    }
    const std::string &payment_id = sdm.paymentId;
    LOG_PRINT_L0("sale details received from multicast: " << sdm.paymentId);
    // TODO: should be signed by sender??
    if (!ctx.global.hasKey(payment_id + CONTEXT_KEY_SALE)) {
        // TODO: clenup after payment done;
        ctx.global[payment_id + CONTEXT_KEY_SALE] = sdm.sale_data;
        ctx.global[payment_id + CONTEXT_KEY_STATUS] = sdm.status;
        ctx.global[payment_id + CONTEXT_KEY_SALE_DETAILS] = sdm.details;
    } else {
        LOG_PRINT_L0("payment " << payment_id << " already known");
    }

    MulticastResponseToCryptonodeJsonRpc resp;
    resp.result.status = "OK";
    output.load(resp);

    return Status::Ok;
}


void registerSaleRequest(graft::Router &router)
{
    Router::Handler3 h1(nullptr, saleClientHandler, nullptr);
    router.addRoute("/sale", METHOD_POST, h1);
    Router::Handler3 h2(nullptr, saleCryptonodeHandler, nullptr);
    router.addRoute("/cryptonode/sale", METHOD_POST, h2);
}

}
