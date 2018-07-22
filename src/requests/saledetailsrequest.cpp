#include "saledetailsrequest.h"
#include "requestdefines.h"
#include "jsonrpc.h"
#include "router.h"
#include "rta/fullsupernodelist.h"
#include "rta/supernode.h"
#include "requests/unicast.h"
#include "utils/utils.h"

namespace graft {

// json-rpc request from POS
GRAFT_DEFINE_JSON_RPC_REQUEST(SaleDetailsRequestJsonRpc, SaleDetailsRequest);

// json-rpc response to POS
GRAFT_DEFINE_JSON_RPC_RESPONSE_RESULT(SaleDetailsResponseJsonRpc, SaleDetailsResponse);


enum class SaleDetailsHandlerState : int {
    ClientRequest = 0,
    ResponseFromAuthSample
};

bool fillAuthSampleWithFees(const SaleDetailsRequest &req, graft::Context &ctx, SaleDetailsResponse &resp, JsonRpcError &error,
                            std::vector<SupernodePtr> &authSample)
{
    FullSupernodeListPtr fsl = ctx.global.get(CONTEXT_KEY_FULLSUPERNODELIST, FullSupernodeListPtr());
    // generate auth sample

    if (!fsl->buildAuthSample(req.BlockNumber, authSample)) {
        error.code = ERROR_INVALID_PARAMS;
        error.message  = MESSAGE_RTA_CANT_BUILD_AUTH_SAMPLE;
        return false;
    }
    for (const auto &member : authSample) {
        SupernodeFee snf;
        snf.address = member->walletAddress();
        snf.fee = 0; // TODO: how do we get fee amounts or percentage
        resp.AuthSampleFees.push_back(snf);
    }

}

Status handleClientRequest(const Router::vars_t& vars, const graft::Input& input,
                           graft::Context& ctx, graft::Output& output)
{
    ctx.local[__FUNCTION__] = SaleDetailsHandlerState::ResponseFromAuthSample;
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

    const SaleDetailsRequest &in = req.params;

    do {
        if (in.PaymentID.empty())
        {
            return errorInvalidPaymentID(output);
        }

        int current_status = ctx.global.get(in.PaymentID + CONTEXT_KEY_STATUS, static_cast<int>(RTAStatus::None));

        if (errorFinishedPayment(current_status, output))
        {
            // return Status::Error;
        }
        SaleDetailsResponseJsonRpc out;
        std::vector<SupernodePtr> authSample;
        fillAuthSampleWithFees(in, ctx, out.result, error, authSample);
        // we have sale details locally, easy way
        if (ctx.global.hasKey(in.PaymentID + CONTEXT_KEY_SALE_DETAILS)) {
            LOG_PRINT_L0("we have sale details locally for payment id: " << in.PaymentID);
            std::string details = ctx.global.get(in.PaymentID + CONTEXT_KEY_SALE_DETAILS, std::string());
            out.result.Details = details;
            output.load(out);
            return Status::Ok;
        } else {
        // we don't have a sale details, request it from remote supernode
            LOG_PRINT_L0("we DON'T have sale details locally for payment id: " << in.PaymentID);
            Output innerOut;
            innerOut.loadT<serializer::JSON_B64>(in);
            UnicastRequestJsonRpc unicastReq;
            SupernodePtr supernode = ctx.global.get(CONTEXT_KEY_SUPERNODE, SupernodePtr());
            unicastReq.params.sender_address = supernode->walletAddress();
            int maxIndex = authSample.size() - 1;
            unicastReq.params.receiver_address = authSample.at(utils::random_number(0, maxIndex))->walletAddress();
            unicastReq.params.data = innerOut.data();
            unicastReq.params.wait_answer = true;
            unicastReq.method = "unicast";

            output.load(unicastReq);
            output.uri = ctx.global.getConfig()->cryptonode_rpc_address + "/json_rpc/rta";
            LOG_PRINT_L0("calling cryptonode: " << output.uri);
            LOG_PRINT_L0("\t with data: " << output.data());
        }
    } while (false);

    if (error.code != 0) {
        output.load(error);
        return Status::Error;
    }

    return Status::Forward;
}

Status handleSaleDetailsResponse(const Router::vars_t& vars, const graft::Input& input,
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

//    SaleDetailsResponseJsonRpc out;
//    std::vector<SupernodePtr> authSample;
//    fillAuthSampleWithFees(in, ctx, out.result, error, authSample);
//    // we have sale details locally, easy way
//    if (ctx.global.hasKey(in.PaymentID + CONTEXT_KEY_SALE_DETAILS)) {
//        std::string details = ctx.global.get(in.PaymentID + CONTEXT_KEY_SALE_DETAILS, std::string());
//        out.result.Details = details;
//        output.load(out);
//        return Status::Ok;

    LOG_PRINT_L0(__FUNCTION__ << " data: " << input.data());
    output.load(error);
    return Status::Ok;

}

Status saleDetailsHandler(const Router::vars_t& vars, const graft::Input& input,
                                graft::Context& ctx, graft::Output& output)
{



    SaleDetailsHandlerState state = ctx.local.hasKey(__FUNCTION__) ? ctx.local[__FUNCTION__] : SaleDetailsHandlerState::ClientRequest;

    switch (state) {
    case SaleDetailsHandlerState::ClientRequest:
        return handleClientRequest(vars, input, ctx, output);
    case SaleDetailsHandlerState::ResponseFromAuthSample:
        return handleSaleDetailsResponse(vars, input, ctx, output);
    }




}

void registerSaleDetailsRequest(Router &router)
{
    Router::Handler3 h3(nullptr, saleDetailsHandler, nullptr);
    router.addRoute("/sale_details", METHOD_POST, h3);
}

}
