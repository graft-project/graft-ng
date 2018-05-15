#include "saledetailsrequest.h"
#include "requestdefines.h"

namespace graft {

Router::Status saleDetailsHandler(const Router::vars_t& vars, const graft::Input& input,
                                  graft::Context& ctx, graft::Output& output)
{
    SaleDetailsRequest in = input.get<SaleDetailsRequest>();
    if (in.PaymentID.empty() || !ctx.global.hasKey(in.PaymentID + CONTEXT_KEY_STATUS))
    {
        return errorInvalidPaymentID(output);
    }
    int current_status = ctx.global[in.PaymentID + CONTEXT_KEY_STATUS];
    if (errorFinishedPayment(current_status, output))
    {
        return Router::Status::Error;
    }
    SaleDetailsResponse out;
    if (ctx.global.hasKey(in.PaymentID + CONTEXT_KEY_SALE_DETAILS))
    {
        std::string details = ctx.global[in.PaymentID + CONTEXT_KEY_SALE_DETAILS];
        out.Details = details;
    }
    output.load(out);
    return Router::Status::Ok;
}

void registerSaleDetailsRequest(Router &router)
{
    Router::Handler3 h3(nullptr, saleDetailsHandler, nullptr);
    router.addRoute("/dapi/sale_details", METHOD_POST, h3);
}

}
