#include "paystatusrequest.h"
#include "requestdefines.h"

namespace graft {

Status payStatusHandler(const Router::vars_t& vars, const graft::Input& input,
                        graft::Context& ctx, graft::Output& output)
{
    PayStatusRequest in = input.get<PayStatusRequest>();
    int current_status = ctx.global.get(in.PaymentID + CONTEXT_KEY_STATUS, static_cast<int>(RTAStatus::None));
    if (in.PaymentID.empty() || current_status == 0)
    {
        return errorInvalidPaymentID(output);
    }
    PayStatusResponse out;
    out.Status = current_status;
    output.load(out);
    return Status::Ok;
}

void registerPayStatusRequest(Router &router)
{
    Router::Handler3 h3(payStatusHandler, nullptr, nullptr);
    router.addRoute("/dapi/pay_status", METHOD_POST, h3);
}

}
