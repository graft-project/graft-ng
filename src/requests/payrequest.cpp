#include "payrequest.h"
#include "requestdefines.h"
#include "requesttools.h"

namespace graft {

Status payWorkerHandler(const Router::vars_t& vars, const graft::Input& input,
                        graft::Context& ctx, graft::Output& output)
{
    PayRequest in = input.get<PayRequest>();
    int current_status = ctx.global.get(in.PaymentID + CONTEXT_KEY_STATUS, static_cast<int>(RTAStatus::None));
    if (!in.Address.empty() && !in.Amount.empty()
            && !in.PaymentID.empty() && in.BlockNumber >= 0 //TODO: Change to `BlockNumber > 0`
            && current_status != 0)
    {
        if (errorFinishedPayment(current_status, output))
        {
            return Status::Error;
        }
        uint64_t amount = convertAmount(in.Amount);
        if (amount <= 0)
        {
            return errorInvalidAmount(output);
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
        return Status::Ok;
    }
    return errorInvalidParams(output);
}

void registerPayRequest(Router &router)
{
    Router::Handler3 h3(nullptr, payWorkerHandler, nullptr);
    router.addRoute("/dapi/pay", METHOD_POST, h3);
}

}
