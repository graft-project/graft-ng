#include "salestatusrequest.h"
#include "requestdefines.h"
#include "requests/broadcast.h"

namespace graft {

// json-rpc request from client
GRAFT_DEFINE_JSON_RPC_REQUEST(SaleStatusRequestJsonRpc, SaleStatusRequest);

// json-rpc response to client
GRAFT_DEFINE_JSON_RPC_RESPONSE_RESULT(SaleStatusResponseJsonRpc, SaleStatusResponse);


Status saleStatusHandler(const Router::vars_t& vars, const graft::Input& input,
                         graft::Context& ctx, graft::Output& output)
{
    SaleStatusRequestJsonRpc req;
    if (!input.get(req)) {
        return errorInvalidParams(output);
    }

    const SaleStatusRequest &in = req.params;
    int current_status = ctx.global.get(in.PaymentID + CONTEXT_KEY_STATUS, static_cast<int>(RTAStatus::None));
    if (in.PaymentID.empty() || current_status == 0)
    {
        return errorInvalidPaymentID(output);
    }

    SaleStatusResponseJsonRpc out;
    out.result.Status = current_status;
    output.load(out);
    return Status::Ok;
}

Status updateSaleStatusHandler(const Router::vars_t& vars, const graft::Input& input,
                         graft::Context& ctx, graft::Output& output)
{

    SaleStatusRequestJsonRpc req;

    if (!input.get(req)) {
        return errorInvalidParams(output);
    }

    const SaleStatusRequest &in = req.params;
    int current_status = ctx.global.get(in.PaymentID + CONTEXT_KEY_STATUS, static_cast<int>(RTAStatus::None));
    if (in.PaymentID.empty() || current_status == 0)
    {
        return errorInvalidPaymentID(output);
    }

    SaleStatusResponseJsonRpc out;
    out.result.Status = current_status;
    output.load(out);
    return Status::Ok;
}



void registerSaleStatusRequest(graft::Router &router)
{
    Router::Handler3 h3(saleStatusHandler, nullptr, nullptr);
    router.addRoute("/sale_status", METHOD_POST, h3);
}

}
