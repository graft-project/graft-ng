#include "saledetailsrequest.h"
#include "requestdefines.h"
#include "jsonrpc.h"
#include "router.h"
#include "rta/fullsupernodelist.h"
#include "rta/supernode.h"
#include "requests/unicast.h"
#include "common/utils.h"


#undef MONERO_DEFAULT_LOG_CATEGORY
#define MONERO_DEFAULT_LOG_CATEGORY "supernode.saledetailsrequest"

namespace graft {

// json-rpc request from POS
GRAFT_DEFINE_JSON_RPC_REQUEST(SaleDetailsRequestJsonRpc, SaleDetailsRequest);

// json-rpc response to POS
GRAFT_DEFINE_JSON_RPC_RESPONSE_RESULT(SaleDetailsResponseJsonRpc, SaleDetailsResponse);




// helper function. prepares sale details response for given request
bool prepareSaleDetailsResponse(const SaleDetailsRequest &req, graft::Context &ctx, SaleDetailsResponse &resp, JsonRpcError &error,
                                const std::vector<SupernodePtr> &authSample)
{
    // generate auth sample
    SaleDetailsResponseJsonRpc out;


    if (!ctx.global.hasKey(req.PaymentID + CONTEXT_KEY_SALE)) {
        error.code = ERROR_PAYMENT_ID_INVALID;
        error.message = string("sale data missing for payment: ") + req.PaymentID;
        LOG_ERROR(__FUNCTION__ << " " << error.message);
        return false;
    }

    resp.Details = ctx.global.get(req.PaymentID + CONTEXT_KEY_SALE_DETAILS, std::string());
    SaleData sale_data = ctx.global.get(req.PaymentID + CONTEXT_KEY_SALE, SaleData());

    uint64_t total_fee = static_cast<uint64_t>(std::round(sale_data.Amount * AUTHSAMPLE_FEE_PERCENTAGE / 100.0));

    for (const auto &member : authSample) {
        SupernodeFee snf;
        snf.Address = member->walletAddress();
        snf.Fee = std::to_string(total_fee / authSample.size());
        resp.AuthSample.push_back(snf);
    }

    return true;
}

Status handleClientRequest(const Router::vars_t& vars, const graft::Input& input,
                           graft::Context& ctx, graft::Output& output)
{
    // in case we have sale data locally, just return it to the client;
    // in case we don't have:
    // 1. build auth sample for given block
    // 2. randomly select one of the member of the auth sample
    // 3. call sale_details as unicast - handle reply and return to the client;
    JsonRpcError error;
    error.code = 0;

    SaleDetailsRequestJsonRpc req;
    if (!input.get(req)) {
        return errorInvalidParams(output);
    }

    SaleDetailsRequest in = req.params;


    if (in.PaymentID.empty())
    {
        return errorInvalidPaymentID(output);
    }

    int current_status = ctx.global.get(in.PaymentID + CONTEXT_KEY_STATUS, static_cast<int>(RTAStatus::None));

    if (errorFinishedPayment(current_status, output))
    {
        return Status::Error;
    }
    // check if we have cached response
    if (ctx.global.hasKey(in.PaymentID + CONTEXT_SALE_DETAILS_RESULT)) {
        SaleDetailsResponse sdr = ctx.global.get(in.PaymentID + CONTEXT_SALE_DETAILS_RESULT, SaleDetailsResponse());
        SaleDetailsResponseJsonRpc out;
        out.result = sdr;
        output.load(out);
        return Status::Ok;
    }

    vector<SupernodePtr> authSample;
    FullSupernodeListPtr fsl = ctx.global.get(CONTEXT_KEY_FULLSUPERNODELIST, FullSupernodeListPtr());

    if (!fsl->buildAuthSample(in.BlockNumber, authSample)) {
        return  errorBuildAuthSample(output);
    }

    // we have sale details locally, easy way
    bool have_data_locally = ctx.global.hasKey(in.PaymentID + CONTEXT_KEY_SALE_DETAILS);

    if (have_data_locally) {
        LOG_PRINT_L0("we have sale details locally for payment id: " << in.PaymentID);
        SaleDetailsResponse sdr;
        SaleDetailsResponseJsonRpc out;
        if (!prepareSaleDetailsResponse(in, ctx, sdr, error, authSample)) {
            JsonRpcErrorResponse er;
            er.error = error;
            output.load(error);
            return Status::Error;
        } else {
            out.result = sdr;
            output.load(out);
            return Status::Ok;
        }
    } else {
        // we don't have a sale details, request it from remote supernode
        LOG_PRINT_L0("we DON'T have sale details locally for payment id: " << in.PaymentID);
        // store payment id so we can cache sale_details from remote supernode
        ctx.local["payment_id"] = in.PaymentID;
        Output innerOut;
        in.callback_uri = "/cryptonode/callback/sale_details/" + boost::uuids::to_string(ctx.getId());
        innerOut.loadT<serializer::JSON_B64>(in);
        UnicastRequestJsonRpc unicastReq;
        SupernodePtr supernode = ctx.global.get(CONTEXT_KEY_SUPERNODE, SupernodePtr());
        unicastReq.params.sender_address = supernode->walletAddress();
        size_t maxIndex = authSample.size() - 1;
        size_t randomIndex = utils::random_number<size_t>(0, maxIndex);
        unicastReq.params.receiver_address = authSample.at(randomIndex)->walletAddress();
        unicastReq.params.data = innerOut.data();
        unicastReq.params.callback_uri = "/cryptonode/sale_details/";
        unicastReq.method = "unicast";

        output.load(unicastReq);
        output.path = "/json_rpc/rta";
        LOG_PRINT_L0(__FUNCTION__ << ", remote address: " << unicastReq.params.receiver_address);
        LOG_PRINT_L0(__FUNCTION__ << ", callback uri: " << in.callback_uri);
        LOG_PRINT_L0("calling cryptonode: " << output.path);
        LOG_PRINT_L0("\t with data: " << output.data());
    }


    return Status::Forward;
}

// unicast response from remote supernode (via cryptonode)
// this function called in "Postponed" state.
// returns output to the waiting client
Status handleSaleDetailsResponse(const Router::vars_t& vars, const graft::Input& input,
                           graft::Context& ctx, graft::Output& output)
{

    if (ctx.local.getLastStatus() != Status::Postpone) {
        string msg = string("Expected postponed status but status is : " + to_string(int(ctx.local.getLastStatus())));
        LOG_ERROR(msg);
        return errorInternalError(msg, output);
    }

    string task_id = boost::uuids::to_string(ctx.getId());
    if (!ctx.global.hasKey(task_id + CONTEXT_SALE_DETAILS_RESULT)) {
        string msg = "no sale details response found for id: " + task_id;
        LOG_ERROR(msg);
        return errorInternalError(msg, output);
    }

    Input inputLocal;
    inputLocal.load(ctx.global.get(task_id + CONTEXT_SALE_DETAILS_RESULT, string()));
    UnicastRequestJsonRpc in;

    if (!inputLocal.get(in)) {
        LOG_ERROR("Failed to parse response: " << inputLocal.data());
        return errorInternalError("Failed to parse response", output);
    }

    UnicastRequest unicastReq = in.params;
    SupernodePtr supernode = ctx.global.get(CONTEXT_KEY_SUPERNODE, SupernodePtr());

    if (unicastReq.receiver_address != supernode->walletAddress()) {
        string msg =  string("wrong receiver address: " + unicastReq.receiver_address + ", expected address: " + supernode->walletAddress());
        LOG_ERROR(msg);
        return errorInternalError(msg, output);
    }

    Input innerIn;
    innerIn.load(unicastReq.data);

    SaleDetailsResponse sdr;

    if (!innerIn.getT<serializer::JSON_B64>(sdr)) {
        LOG_ERROR("error deserialize rta auth response");
        return errorInvalidParams(output);
    }

    string payment_id = ctx.local["payment_id"];

    // cache response;
    ctx.global.set(payment_id + CONTEXT_SALE_DETAILS_RESULT, sdr, RTA_TX_TTL);

    // remove callback reply
    ctx.global.remove(task_id + CONTEXT_SALE_DETAILS_RESULT);

    // send response to the client
    output.load(sdr);
    return Status::Ok;

}

// handles cryptonode's "ok"
Status handleUnicastAcknowledge(const Router::vars_t& vars, const graft::Input& input,
                                graft::Context& ctx, graft::Output& output)
{
    UnicastResponseFromCryptonodeJsonRpc resp;

    JsonRpcErrorResponse error;
    if (!input.get(resp) || resp.error.code != 0 || resp.result.status != STATUS_OK) {
        error.error.code = ERROR_INTERNAL_ERROR;
        error.error.message = "Error unicasting request";
        output.load(error);
        return Status::Error;
    }

    return Status::Postpone; // waiting for callback
}

// handles remote "sale_details"

Status handleSaleDetailsUnicastRequest(const Router::vars_t& vars, const graft::Input& input,
                                        graft::Context& ctx, graft::Output& output)
{
    UnicastRequestJsonRpc in;

    if (!input.get(in)) {
        LOG_ERROR("Failed to parse response: " << input.data());
        return sendOkResponseToCryptonode(output); // cryptonode doesn't care about any errors, it's job is only deliver request
    }

    UnicastRequest unicastReq = in.params;
    SupernodePtr supernode = ctx.global.get(CONTEXT_KEY_SUPERNODE, SupernodePtr());

    if (unicastReq.receiver_address != supernode->walletAddress()) {
        string msg =  string("wrong receiver address: " + supernode->walletAddress());
        LOG_ERROR(msg);
        return sendOkResponseToCryptonode(output); // cryptonode doesn't care about any errors, it's job is only deliver request
    }


    Input innerIn;
    innerIn.load(unicastReq.data);

    SaleDetailsRequest sdr;

    if (!innerIn.getT<serializer::JSON_B64>(sdr)) {
        LOG_ERROR("error deserialize rta auth response");
        return sendOkResponseToCryptonode(output); // cryptonode doesn't care about any errors, it's job is only deliver request
    }

    vector<SupernodePtr> authSample;
    FullSupernodeListPtr fsl = ctx.global.get(CONTEXT_KEY_FULLSUPERNODELIST, FullSupernodeListPtr());

    if (!fsl->buildAuthSample(sdr.BlockNumber, authSample)) {
        return sendOkResponseToCryptonode(output); // cryptonode doesn't care about any errors, it's job is only deliver request
    }

    if (ctx.global.hasKey(sdr.PaymentID + CONTEXT_KEY_SALE_DETAILS)) {
        LOG_PRINT_L0("we have sale details for payment id: " << sdr.PaymentID);
        SaleDetailsResponse resp;

        JsonRpcError error;
        if (!prepareSaleDetailsResponse(sdr, ctx, resp, error, authSample)) {
            LOG_ERROR("Error preparing sale details response");
            return sendOkResponseToCryptonode(output); // cryptonode doesn't care about any errors, it's job is only deliver request
        } else {
            UnicastRequestJsonRpc callbackReq;
            Output innerOut;
            innerOut.loadT<serializer::JSON_B64>(resp);

            callbackReq.params.data = innerOut.data();
            callbackReq.params.callback_uri = sdr.callback_uri;
            callbackReq.params.sender_address = supernode->walletAddress();
            callbackReq.params.receiver_address = unicastReq.sender_address;
            callbackReq.method = "unicast";
            output.load(callbackReq);
            output.path = "/json_rpc/rta";
            LOG_PRINT_L0(__FUNCTION__ << ", performing sale details callback, remote URI: " << sdr.callback_uri << ", remote addr: " << unicastReq.sender_address);
            return Status::Forward;
        }
    }
    LOG_ERROR("no sale details for payment: " << sdr.PaymentID);
    return sendOkResponseToCryptonode(output);
}




// ========================================================================================================

// handlers for
// 1) client requests
// 2) callbacks with response
// 3) requests from remote cryptonode

// handles client requests
Status saleDetailsClientHandler(const Router::vars_t& vars, const graft::Input& input,
                                graft::Context& ctx, graft::Output& output)
{

    enum class ClientHandlerState : int {
        ClientRequest = 0,      // request from client (initial state)
        UnicastAcknowledge,     // unicast to cryptonode delivered
        CallbackFromAuthSample, // response from remote supernode (random auth sample member)
    };

    ClientHandlerState state = ctx.local.hasKey(__FUNCTION__) ? ctx.local[__FUNCTION__] : ClientHandlerState::ClientRequest;
    LOG_PRINT_L0(__FUNCTION__ << " state: " << int(state) << ", task_id: " << boost::uuids::to_string(ctx.getId()));
    switch (state) {
    case ClientHandlerState::ClientRequest:
        ctx.local[__FUNCTION__] = ClientHandlerState::UnicastAcknowledge;
        return handleClientRequest(vars, input, ctx, output);
    case ClientHandlerState::UnicastAcknowledge:
        ctx.local[__FUNCTION__] = ClientHandlerState::CallbackFromAuthSample;
        return handleUnicastAcknowledge(vars, input, ctx, output);
    case ClientHandlerState::CallbackFromAuthSample:
        return handleSaleDetailsResponse(vars, input, ctx, output);
    }
}

// handles callback with response from remote supernode
// /cryptonode/callback/sale_details/{id:[0-9a-fA-F-]+}
// there's no states for this handler, only one state
Status saleDetailsCallbackHandler(const Router::vars_t& vars, const graft::Input& input,
                                  graft::Context& ctx, graft::Output& output)
{

    if (vars.count("id") == 0) {
        string msg = string("Can't parse request id from URL");
        LOG_ERROR(msg);
        return errorInternalError(msg, output);
    }

    std::string id = vars.find("id")->second;
    boost::uuids::string_generator sg;
    boost::uuids::uuid uuid = sg(id);
    ctx.global.set(id + CONTEXT_SALE_DETAILS_RESULT, input.data(), RTA_TX_TTL);
    ctx.setNextTaskId(uuid);
    return graft::Status::Ok; // initial handler will be called (clientHandler)
}


// handles unicast requests from remote cryptonode.
Status saleDetailsUnicastHandler(const Router::vars_t& vars, const graft::Input& input,
                                 graft::Context& ctx, graft::Output& output)
{
    enum class State: int {
        ClientRequest = 0,    // requests comes from cryptonode
        CallbackToClient,     // unicast callback sent to cryptonode
        CallbackAcknowledge,  // cryptonode accepted callbacks,
    };

    State state = ctx.local.hasKey(__FUNCTION__) ? ctx.local[__FUNCTION__] : State::ClientRequest;

    LOG_PRINT_L0(__FUNCTION__ << " state: " << int(state));

    switch (state) {
    case State::ClientRequest:
        ctx.local[__FUNCTION__] = State::CallbackToClient;
        return handleSaleDetailsUnicastRequest(vars, input, ctx, output); // send Unicast callback
    case State::CallbackToClient:
        ctx.local[__FUNCTION__] = State::CallbackAcknowledge;
        return sendOkResponseToCryptonode(output);                       //  cryptonode accepted uncast callback,
    case State::CallbackAcknowledge:
        ctx.local[__FUNCTION__] = State::CallbackAcknowledge;
        return sendOkResponseToCryptonode(output);                       // send ok as reply to initial request
    }
}


void registerSaleDetailsRequest(Router &router)
{
    // client requests
    Router::Handler3 clientHandler(nullptr, saleDetailsClientHandler, nullptr);
    router.addRoute("/sale_details", METHOD_POST, clientHandler);

    // unicast callbacks from remote supernode (responses)
    Router::Handler3 callbackHandler(nullptr, saleDetailsCallbackHandler, nullptr);
    router.addRoute("/cryptonode/callback/sale_details/{id:[0-9a-fA-F-]+}",
                    METHOD_POST, callbackHandler);

    // unicast requests from remote supernode (requests)
    Router::Handler3 unicastRequestHandler(nullptr, saleDetailsUnicastHandler, nullptr);
    router.addRoute("/cryptonode/sale_details/",
                    METHOD_POST, unicastRequestHandler);
}

}
