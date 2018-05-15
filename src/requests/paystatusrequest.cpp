#include "paystatusrequest.h"
#include "requestdefines.h"

namespace graft {

Status payStatusHandler(const Router::vars_t& vars, const graft::Input& input,
                        graft::Context& ctx, graft::Output& output)
{
    PayStatusRequest in = input.get<PayStatusRequest>();
    if (in.PaymentID.empty() || !ctx.global.hasKey(in.PaymentID + CONTEXT_KEY_STATUS))
    {
        return errorInvalidPaymentID(output);
    }
    PayStatusResponse out;
    out.Status = ctx.global[in.PaymentID + CONTEXT_KEY_STATUS];
    output.load(out);
    return Status::Ok;
}

void registerPayStatusRequest(Router &router)
{
    Router::Handler3 h3(payStatusHandler, nullptr, nullptr);
    router.addRoute("/dapi/pay_status", METHOD_POST, h3);
}

}
