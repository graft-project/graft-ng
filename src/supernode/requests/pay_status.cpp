
#include "supernode/requests/pay_status.h"
#include "supernode/requestdefines.h"
#include "lib/graft/jsonrpc.h"
#include <misc_log_ex.h>

#undef MONERO_DEFAULT_LOG_CATEGORY
#define MONERO_DEFAULT_LOG_CATEGORY "supernode.paystatusrequest"

namespace graft::supernode::request {

// json-rpc request from client
GRAFT_DEFINE_JSON_RPC_REQUEST(PayStatusRequestJsonRpc, PayStatusRequest);

// json-rpc response to client
GRAFT_DEFINE_JSON_RPC_RESPONSE_RESULT(PayStatusResponseJsonRpc, PayStatusResponse);


Status payStatusHandler(const Router::vars_t& vars, const graft::Input& input,
                        graft::Context& ctx, graft::Output& output)
{
    PayStatusRequestJsonRpc req;
    if (!input.get(req)) {
        return errorInvalidParams(output);
    }

    const PayStatusRequest &in = req.params;

    MDEBUG("requested status for payment: " << in.PaymentID);

    int current_status = ctx.global.get(in.PaymentID + CONTEXT_KEY_STATUS, static_cast<int>(RTAStatus::None));
    if (in.PaymentID.empty() || current_status == 0)
    {
        MWARNING("no status for payment: " << in.PaymentID);
        return errorInvalidPaymentID(output);
    }
    MDEBUG("payment: " << in.PaymentID
           << ", status found: " << current_status);
    PayStatusResponseJsonRpc out;
    out.result.Status = current_status;
    output.load(out);
    return Status::Ok;

}

void registerPayStatusRequest(Router& router)
{
    Router::Handler3 h3(payStatusHandler, nullptr, nullptr);
    router.addRoute("/pay_status", METHOD_POST, h3);
}

}

