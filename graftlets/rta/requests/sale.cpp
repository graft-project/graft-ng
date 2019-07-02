#include "sale.h"
#include "common.h"

#include "supernode/requests/broadcast.h"
#include "supernode/requests/sale_status.h"


#include "supernode/requestdefines.h"
#include "lib/graft/requesttools.h"
#include "rta/supernode.h"
#include "rta/fullsupernodelist.h"

#include <string>


#undef MONERO_DEFAULT_LOG_CATEGORY
#define MONERO_DEFAULT_LOG_CATEGORY "supernode.salerequest"


namespace graft::supernode::request {

const std::chrono::seconds SALE_TTL = std::chrono::seconds(60);

enum class SaleHandlerState : int
{
    ClientRequest = 0,
    SaleMulticastReply
};


Status handleClientSaleRequest(const Router::vars_t& vars, const graft::Input& input, graft::Context& ctx, graft::Output& output)
{
    SaleRequest req;

    if (!input.get(req)) {
        return errorInvalidParams(output);
    }


    if (req.PaymentID.empty()) {
        return errorInvalidPaymentID(output);
    }

    // looks like AuthSampleKeys will not be used as keys are already embedded into encrypted message blob. Handled internally by graft::crypto_tools::encryptMessage
    if (req.paymentData.AuthSampleKeys.size() != FullSupernodeList::AUTH_SAMPLE_SIZE) {
        return errorCustomError(MESSAGE_RTA_INVALID_AUTH_SAMLE, ERROR_INVALID_PARAMS, output);
    }

    if (req.paymentData.EncryptedPayment.empty()) {
        return errorInvalidParams(output);
    }

    // here we need to perform two actions:
    // 1. multicast sale over auth sample
    // 2. broadcast sale status

    ctx.global.set(req.PaymentID + CONTEXT_KEY_PAYMENT_DATA, req.paymentData, SALE_TTL);
    ctx.global.set(req.PaymentID + CONTEXT_KEY_STATUS, static_cast<int>(RTAStatus::Waiting), SALE_TTL);

    SupernodePtr supernode = ctx.global.get(CONTEXT_KEY_SUPERNODE, SupernodePtr());



    Output innerOut;
    innerOut.loadT<serializer::JSON_B64>(req);
    BroadcastRequest bcast;
    bcast.callback_uri = "/core/store_payment_data";
    bcast.sender_address = supernode->idKeyAsString();
    bcast.data = innerOut.data();

#if 0 // broadcast to all while development/debugging // TODO: enable this code
    for (const auto & item : req.paymentData.AuthSampleKeys) {
        bcast.receiver_addresses.push_back(item.Id);
    }
#endif
    if (!utils::signBroadcastMessage(bcast, supernode)) {
        return errorInternalError("Failed to sign broadcast message", output);
    }

    BroadcastRequestJsonRpc cryptonode_req;
    cryptonode_req.method = "broadcast";
    cryptonode_req.params = std::move(bcast);

    output.load(cryptonode_req);
    output.path = "/json_rpc/rta";
    MDEBUG("multicasting: " << output.data());
    return Status::Forward;
}


Status handleSaleMulticastReply(const Router::vars_t& vars, const graft::Input& input,
                                graft::Context& ctx, graft::Output& output)
{
    // check cryptonode reply
    MDEBUG(__FUNCTION__ << " begin");
    BroadcastResponseFromCryptonodeJsonRpc resp;

    JsonRpcErrorResponse error;
    if (!input.get(resp) || resp.error.code != 0 || resp.result.status != STATUS_OK) {
        return errorCustomError("Error multicasting request", ERROR_INTERNAL_ERROR, output);
    }
    output.reset();
    output.resp_code = 202;
    return Status::Ok;
}

/*!
 * \brief handleSaleRequest - handles /dapi/v2.0/sale POS request
 * \param vars
 * \param input
 * \param ctx
 * \param output
 * \return
 */
Status handleSaleRequest(const Router::vars_t& vars, const graft::Input& input,
                         graft::Context& ctx, graft::Output& output)
{

    SaleHandlerState state = ctx.local.hasKey(__FUNCTION__) ? ctx.local[__FUNCTION__] : SaleHandlerState::ClientRequest;
    // state machine to perform a) multicast to cryptonode
    switch (state) {
    // client requested "/sale"
    case SaleHandlerState::ClientRequest:
        ctx.local[__FUNCTION__] = SaleHandlerState::SaleMulticastReply;
        // call cryptonode's "/rta/multicast" to send sale data to auth sample
        // "handleClientSaleRequest" returns Forward;
        return handleClientSaleRequest(vars, input, ctx, output);
    case SaleHandlerState::SaleMulticastReply:
        // handle "multicast" response from cryptonode, check it's status, send
        // "sale status" with broadcast to cryptonode
        MDEBUG("SaleMulticast response from cryptonode: " << input.data());
        MDEBUG("status: " << (int)ctx.local.getLastStatus());
        return handleSaleMulticastReply(vars, input, ctx, output);
    default:
        LOG_ERROR("Internal error: unhandled state");
        abort();
    };

}


//void registerSaleRequest(graft::Router &router)
//{
//    Router::Handler3 h1(nullptr, saleClientHandler, nullptr);
//    router.addRoute("/sale", METHOD_POST, h1);
//    Router::Handler3 h2(nullptr, saleCryptonodeHandler, nullptr);
//    router.addRoute("/cryptonode/sale", METHOD_POST, h2);
//}

}

