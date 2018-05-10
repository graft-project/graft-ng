#include "salerequest.h"
#include "requestdefines.h"
#include "requesttools.h"

namespace graft {

Router::Status saleWorkerHandler(const Router::vars_t& vars, const graft::Input& input,
                                 graft::Context& ctx, graft::Output& output)
{
    SaleRequest in = input.get<SaleRequest>();
    if (!in.Address.empty() && !in.Amount.empty())
    {
        std::string payment_id = generatePaymentID();
        if (ctx.global.hasKey(payment_id + CONTEXT_KEY_SALE))
        {
            ErrorResponse err;
            err.code = ERROR_SALE_REQUEST_FAILED;
            err.message = MESSAGE_SALE_REQUEST_FAILED;
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
        SaleData data(in.Address, 0, amount);
        ctx.global[payment_id + CONTEXT_KEY_SALE] = data;
        ctx.global[payment_id + CONTEXT_KEY_SALE_STATUS] = RTAStatus::InProgress;
        if (!in.SaleDetails.empty())
        {
            ctx.global[payment_id + CONTEXT_KEY_SALE_DETAILS] = in.SaleDetails;
        }
        SaleResponse out;
        out.BlockNumber = data.BlockNumber;
        out.PaymentID = payment_id;
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

void registerSaleRequest(graft::Router &router)
{
    static Router::Handler3 h3_test(nullptr, saleWorkerHandler, nullptr);
    router.addRoute("/dapi/sale", METHOD_POST, h3_test);
}

}
