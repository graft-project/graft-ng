#include "salerequest.h"
#include "requestdefines.h"
#include "requesttools.h"
#include "rta/supernode.h"
#include "rta/fullsupernodelist.h"


namespace graft {

Status saleWorkerHandler(const Router::vars_t& vars, const graft::Input& input,
                                 graft::Context& ctx, graft::Output& output)
{
    SaleRequest in = input.get<SaleRequest>();
    if (!in.Address.empty() && !in.Amount.empty())
    {
        std::string payment_id = in.PaymentID;
        if (payment_id.empty()) // request comes from POS.
        {
            payment_id = generatePaymentID();
        } else { // request comes from

        }
        if (ctx.global.hasKey(payment_id + CONTEXT_KEY_SALE))
        {
            ErrorResponse err;
            err.code = ERROR_SALE_REQUEST_FAILED;
            err.message = MESSAGE_SALE_REQUEST_FAILED;
            output.load(err);
            return Status::Error;
        }
        uint64_t amount = convertAmount(in.Amount);
        if (amount <= 0)
        {
            return errorInvalidAmount(output);
        }

        if (!Supernode::validateAddress(in.Address, ctx.global.getConfig()->testnet))
        {
            ErrorResponse err;
            err.code = ERROR_ADDRESS_INVALID;
            err.message = MESSAGE_ADDRESS_INVALID;
            output.load(err);
            return Status::Error;
        }

        FullSupernodeList::SupernodePtr supernode = ctx.global.get(CONTEXT_KEY_SUPERNODE, FullSupernodeList::SupernodePtr());

        SaleData data(in.Address, supernode->daemonHeight(), amount);

        ctx.global[payment_id + CONTEXT_KEY_SALE] = data;
        ctx.global[payment_id + CONTEXT_KEY_STATUS] = static_cast<int>(RTAStatus::Waiting);

        if (!in.SaleDetails.empty())
        {
            ctx.global[payment_id + CONTEXT_KEY_SALE_DETAILS] = in.SaleDetails;
        }
        // TODO: generate auth sample

        // TODO: Sale: Add broadcast and another business logic
        SaleResponse out;
        out.BlockNumber = data.BlockNumber;
        out.PaymentID = payment_id;
        output.load(out);
        return Status::Ok;
    }
    return errorInvalidParams(output);
}

void registerSaleRequest(graft::Router &router)
{
    Router::Handler3 h3(nullptr, saleWorkerHandler, nullptr);
    router.addRoute("/sale", METHOD_POST, h3);
}

}
