#include "paystatusrequest.h"
#include "requestdefines.h"

namespace graft {

Router::Status payStatusHandler(const Router::vars_t& vars, const graft::Input& input,
                                graft::Context& ctx, graft::Output& output)
{
    PayStatusRequest in = input.get<PayStatusRequest>();
    if (in.PaymentID.empty() || !ctx.global.hasKey(in.PaymentID + CONTEXT_KEY_STATUS))
    {
        ErrorResponse err;
        err.code = ERROR_PAYMENT_ID_INVALID;
        err.message = MESSAGE_PAYMENT_ID_INVALID;
        output.load(err);
        return Router::Status::Error;
    }
    PayStatusResponse out;
    out.Status = ctx.global[in.PaymentID + CONTEXT_KEY_STATUS];
    output.load(out);
    return Router::Status::Ok;
}

void registerPayStatusRequest(Router &router)
{
    Router::Handler3 h3(nullptr, payStatusHandler, nullptr);
    router.addRoute("/dapi/pay_status", METHOD_POST, h3);
}

}
