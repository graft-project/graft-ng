#include "rejectsalerequest.h"
#include "requestdefines.h"

namespace graft {

Router::Status rejectSaleHandler(const Router::vars_t& vars, const graft::Input& input,
                                 graft::Context& ctx, graft::Output& output)
{
    RejectSaleRequest in = input.get<RejectSaleRequest>();
    if (in.PaymentID.empty() || !ctx.global.hasKey(in.PaymentID + CONTEXT_KEY_STATUS))
    {
        ErrorResponse err;
        err.code = ERROR_PAYMENT_ID_INVALID;
        err.message = MESSAGE_PAYMENT_ID_INVALID;
        output.load(err);
        return Router::Status::Error;
    }
    ctx.global[in.PaymentID + CONTEXT_KEY_STATUS] = static_cast<int>(RTAStatus::RejectedByPOS);
    // TODO: Reject Sale: Add broadcast and another business logic
    RejectSaleResponse out;
    out.Result = STATUS_OK;
    output.load(out);
    return Router::Status::Ok;
}

void registerRejectSaleRequest(Router &router)
{
    Router::Handler3 h3(nullptr, rejectSaleHandler, nullptr);
    router.addRoute("/dapi/reject_sale", METHOD_POST, h3);
}

}
