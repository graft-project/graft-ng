#include "salestatusrequest.h"
#include "requestdefines.h"

namespace graft {

Router::Status saleStatusHandler(const Router::vars_t& vars, const graft::Input& input,
                                 graft::Context& ctx, graft::Output& output)
{
    SaleStatusRequest in = input.get<SaleStatusRequest>();
    if (in.PaymentID.empty() || !ctx.global.hasKey(in.PaymentID + CONTEXT_KEY_STATUS))
    {
        return errorInvalidPaymentID(output);
    }
    SaleStatusResponse out;
    out.Status = ctx.global[in.PaymentID + CONTEXT_KEY_STATUS];
    output.load(out);
    return Router::Status::Ok;
}

void registerSaleStatusRequest(graft::Router &router)
{
    Router::Handler3 h3(nullptr, saleStatusHandler, nullptr);
    router.addRoute("/dapi/sale_status", METHOD_POST, h3);
}

}
