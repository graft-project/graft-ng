#include "saledetailsrequest.h"
#include "requestdefines.h"

namespace graft {

Status saleDetailsHandler(const Router::vars_t& vars, const graft::Input& input,
                          graft::Context& ctx, graft::Output& output)
{
    SaleDetailsRequest in = input.get<SaleDetailsRequest>();
    int current_status = ctx.global.get(in.PaymentID + CONTEXT_KEY_STATUS, static_cast<int>(RTAStatus::None));
    if (in.PaymentID.empty() || current_status == 0)
    {
        return errorInvalidPaymentID(output);
    }
    if (errorFinishedPayment(current_status, output))
    {
        return Status::Error;
    }
    SaleDetailsResponse out;
    std::string details = ctx.global.get(in.PaymentID + CONTEXT_KEY_SALE_DETAILS, std::string());
    out.Details = details;
    output.load(out);
    return Status::Ok;
}

void registerSaleDetailsRequest(Router &router)
{
    Router::Handler3 h3(nullptr, saleDetailsHandler, nullptr);
    router.addRoute("/dapi/sale_details", METHOD_POST, h3);
}

}
