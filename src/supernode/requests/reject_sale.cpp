
#include "supernode/requests/reject_sale.h"
#include "supernode/requestdefines.h"

#undef MONERO_DEFAULT_LOG_CATEGORY
#define MONERO_DEFAULT_LOG_CATEGORY "supernode.rejectsalerequest"

namespace graft::supernode::request {

Status rejectSaleHandler(const Router::vars_t& vars, const graft::Input& input,
                         graft::Context& ctx, graft::Output& output)
{
    RejectSaleRequest in = input.get<RejectSaleRequest>();
    int current_status = ctx.global.get(in.PaymentID + CONTEXT_KEY_STATUS, static_cast<int>(RTAStatus::None));
    if (in.PaymentID.empty() || current_status == 0)
    {
        return errorInvalidPaymentID(output);
    }
    ctx.global[in.PaymentID + CONTEXT_KEY_STATUS] = static_cast<int>(RTAStatus::RejectedByPOS);
    // TODO: Reject Sale: Add broadcast and another business logic
    RejectSaleResponse out;
    out.Result = STATUS_OK;
    output.load(out);
    return Status::Ok;
}

void registerRejectSaleRequest(Router& router)
{
    Router::Handler3 h3(nullptr, rejectSaleHandler, nullptr);
    router.addRoute("/reject_sale", METHOD_POST, h3);
}

}

