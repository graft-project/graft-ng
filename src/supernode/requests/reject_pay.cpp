
#include "supernode/requests/reject_pay.h"
#include "supernode/requestdefines.h"

#undef MONERO_DEFAULT_LOG_CATEGORY
#define MONERO_DEFAULT_LOG_CATEGORY "supernode.rejectpayrequest"

namespace graft::supernode::request {

Status rejectPayHandler(const Router::vars_t& vars, const graft::Input& input,
                        graft::Context& ctx, graft::Output& output)
{
    RejectPayRequest in = input.get<RejectPayRequest>();
    int current_status = ctx.global.get(in.PaymentID + CONTEXT_KEY_STATUS, static_cast<int>(RTAStatus::None));
    if (in.PaymentID.empty() || current_status == 0)
    {
        return errorInvalidPaymentID(output);
    }
    ctx.global[in.PaymentID + CONTEXT_KEY_STATUS] = static_cast<int>(RTAStatus::RejectedByWallet);
    // TODO: Reject Pay: Add broadcast and another business logic
    RejectPayResponse out;
    out.Result = STATUS_OK;
    output.load(out);
    return Status::Ok;
}

void registerRejectPayRequest(Router &router)
{
    Router::Handler3 h3(nullptr, rejectPayHandler, nullptr);
    router.addRoute("/reject_pay", METHOD_POST, h3);
}

}

