#include "saledetailsrequest.h"
#include "requestdefines.h"

namespace graft {

Router::Status saleDetailsHandler(const Router::vars_t& vars, const graft::Input& input,
                                  graft::Context& ctx, graft::Output& output)
{
    SaleDetailsRequest in = input.get<SaleDetailsRequest>();
    if (in.PaymentID.empty() || !ctx.global.hasKey(in.PaymentID + CONTEXT_KEY_STATUS))
    {
        ErrorResponse err;
        err.code = ERROR_PAYMENT_ID_INVALID;
        err.message = MESSAGE_PAYMENT_ID_INVALID;
        output.load(err);
        return Router::Status::Error;
    }
    int current_status = ctx.global[in.PaymentID + CONTEXT_KEY_STATUS];
    if (current_status != static_cast<int>(RTAStatus::Waiting)
            && current_status != static_cast<int>(RTAStatus::InProgress))
    {
        ErrorResponse err;
        if (current_status == static_cast<int>(RTAStatus::Success))
        {
            err.code = ERROR_RTA_COMPLETED;
            err.message = MESSAGE_RTA_COMPLETED;
        }
        else
        {
            err.code = ERROR_RTA_FAILED;
            err.message = MESSAGE_RTA_FAILED;
        }
        output.load(err);
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
