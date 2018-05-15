#include "payrequest.h"
#include "requestdefines.h"
#include "requesttools.h"

namespace graft {

Router::Status payWorkerHandler(const Router::vars_t& vars, const graft::Input& input,
                                graft::Context& ctx, graft::Output& output)
{
    PayRequest in = input.get<PayRequest>();
    if (!in.Address.empty() && !in.Amount.empty()
            && !in.PaymentID.empty() && in.BlockNumber >= 0 //TODO: Change to `BlockNumber > 0`
            && ctx.global.hasKey(in.PaymentID + CONTEXT_KEY_STATUS))
    {
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
        uint64_t amount = convertAmount(in.Amount);
        if (amount <= 0)
        {
            ErrorResponse err;
            err.code = ERROR_AMOUNT_INVALID;
            err.message = MESSAGE_AMOUNT_INVALID;
            output.load(err);
            return Router::Status::Error;
        }
        // TODO: Validate address
        PayData data(in.Address, 0, amount); // TODO: Use correct BlockNumber
        ctx.global[in.PaymentID + CONTEXT_KEY_PAY] = data;
        ctx.global[in.PaymentID + CONTEXT_KEY_STATUS] = static_cast<int>(RTAStatus::InProgress);
        // TODO: Sale: Add broadcast and another business logic
        // TODO: Temporary solution:
        ctx.global[in.PaymentID + CONTEXT_KEY_STATUS] = static_cast<int>(RTAStatus::Success);
        // TODO END
        PayResponse out;
        out.Result = STATUS_OK;
        output.load(out);
        return Router::Status::Ok;
    }
    else
    {
        ErrorResponse err;
        err.code = ERROR_INVALID_PARAMS;
        err.message = MESSAGE_INVALID_PARAMS;
        output.load(err);
        return Router::Status::Error;
    }
}

void registerPayRequest(Router &router)
{
    Router::Handler3 h3(nullptr, payWorkerHandler, nullptr);
    router.addRoute("/dapi/pay", METHOD_POST, h3);
}

}
