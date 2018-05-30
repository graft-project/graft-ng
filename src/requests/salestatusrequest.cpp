#include "salestatusrequest.h"
#include "requestdefines.h"

namespace graft {

Status saleStatusHandler(const Router::vars_t& vars, const graft::Input& input,
                         graft::Context& ctx, graft::Output& output)
{
    SaleStatusRequest in = input.get<SaleStatusRequest>();
    int current_status = ctx.global.get(in.PaymentID + CONTEXT_KEY_STATUS, static_cast<int>(RTAStatus::None));
    if (in.PaymentID.empty() || current_status == 0)
    {
        return errorInvalidPaymentID(output);
    }
    SaleStatusResponse out;
    out.Status = current_status;
    output.load(out);
    return Status::Ok;
}

void registerSaleStatusRequest(graft::Router &router)
{
    Router::Handler3 h3(saleStatusHandler, nullptr, nullptr);
    router.addRoute("/dapi/sale_status", METHOD_POST, h3);
}

}
