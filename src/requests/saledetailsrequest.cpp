#include "saledetailsrequest.h"
#include "requestdefines.h"
#include "jsonrpc.h"
#include "router.h"
#include "rta/fullsupernodelist.h"
#include "rta/supernode.h"
#include "requests/multicast.h"

namespace graft {

// json-rpc request from POS
GRAFT_DEFINE_JSON_RPC_REQUEST(SaleDetailsRequestJsonRpc, SaleDetailsRequest);

// json-rpc response to POS
GRAFT_DEFINE_JSON_RPC_RESPONSE_RESULT(SaleDetailsResponseJsonRpc, SaleDetailsResponse);


Status saleDetailsHandler(const Router::vars_t& vars, const graft::Input& input,
                                graft::Context& ctx, graft::Output& output)
{

    JsonRpcError error;
    error.code = 0;

    // validate params, generate payment id, build auth sample, multicast to auth sample

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
            return Status::Error;
        }

        FullSupernodeListPtr fsl = ctx.global.get(CONTEXT_KEY_FULLSUPERNODELIST, FullSupernodeListPtr());
        // generate auth sample
        std::vector<SupernodePtr> authSample;
        if (!fsl->buildAuthSample(in.BlockNumber, authSample)) {
            error.code = ERROR_INVALID_PARAMS;
            error.message  = MESSAGE_RTA_CANT_BUILD_AUTH_SAMPLE;
            break;
        }

        SaleDetailsResponseJsonRpc out;
        std::string details = ctx.global.get(in.PaymentID + CONTEXT_KEY_SALE_DETAILS, std::string());
        out.result.Details = details;

        for (const auto &member : authSample) {
            SupernodeFee snf;
            snf.address = member->walletAddress();
            snf.fee = 0; // TODO: how do we get fee amounts or percentage
            out.result.AuthSampleFees.push_back(snf);
        }
        output.load(out);

    } while (false);

    if (error.code != 0) {
        output.load(error);
        return Status::Error;
    }

    return Status::Ok;

}

void registerSaleDetailsRequest(Router &router)
{
    Router::Handler3 h3(nullptr, saleDetailsHandler, nullptr);
    router.addRoute("/sale_details", METHOD_POST, h3);
}

}
