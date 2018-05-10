#include "salestatusrequest.h"
#include "requestdefines.h"

namespace graft {

Router::Status saleStatusHandler(const Router::vars_t& vars, const graft::Input& input,
                                 graft::Context& ctx, graft::Output& output)
{
    SaleStatusRequest in = input.get<SaleStatusRequest>();
    if (in.PaymentID.empty() || !ctx.global.hasKey(in.PaymentID + CONTEXT_KEY_SALE_STATUS))
    {
        ErrorResponse err;
        err.code = ERROR_PAYMENT_ID_INVALID;
        err.message = MESSAGE_PAYMENT_ID_INVALID;
        output.load(err);
        return Router::Status::Error;
    }
    SaleStatusResponse out;
    out.Status = ctx.global[in.PaymentID + CONTEXT_KEY_SALE_STATUS];
    output.load(out);
    return Router::Status::Ok;
}

void registerSaleStatusRequest(graft::Router &router)
{
    static Router::Handler3 h3(nullptr, saleStatusHandler, nullptr);
    router.addRoute("/dapi/sale_status", METHOD_POST, h3);
}

}
