#include "requestdefines.h"
#include "jsonrpc.h"
#include "context.h"


namespace graft {

Status errorInvalidPaymentID(Output &output)
{
    JsonRpcError err;
    err.code = ERROR_PAYMENT_ID_INVALID;
    err.message = MESSAGE_PAYMENT_ID_INVALID;
    JsonRpcErrorResponse resp;
    resp.error = err;
    output.load(resp);
    return Status::Error;
}

Status errorInvalidParams(Output &output)
{
    JsonRpcError err;
    err.code = ERROR_INVALID_PARAMS;
    err.message = MESSAGE_INVALID_PARAMS;
    JsonRpcErrorResponse resp;
    resp.error = err;
    output.load(resp);
    return Status::Error;
}

Status errorInvalidAmount(Output &output)
{
    JsonRpcError err;
    err.code = ERROR_AMOUNT_INVALID;
    err.message = MESSAGE_AMOUNT_INVALID;
    JsonRpcErrorResponse resp;
    resp.error = err;
    output.load(resp);
    return Status::Error;
}

Status errorInvalidAddress(Output &output)
{
    JsonRpcError err;
    err.code = ERROR_ADDRESS_INVALID;
    err.message = MESSAGE_ADDRESS_INVALID;
    JsonRpcErrorResponse resp;
    resp.error = err;
    output.load(resp);
    return Status::Error;
}

Status errorBuildAuthSample(Output &output)
{
    JsonRpcError err;
    err.code = ERROR_INTERNAL_ERROR;
    err.message = MESSAGE_RTA_CANT_BUILD_AUTH_SAMPLE;
    JsonRpcErrorResponse resp;
    resp.error = err;
    output.load(resp);
    return Status::Error;
}


bool errorFinishedPayment(int status, Output &output)
{
    if (status != static_cast<int>(RTAStatus::Waiting)
            && status != static_cast<int>(RTAStatus::InProgress))
    {
        JsonRpcError err;
        if (status == static_cast<int>(RTAStatus::Success))
        {
            err.code = ERROR_RTA_COMPLETED;
            err.message = MESSAGE_RTA_COMPLETED;
        }
        else
        {
            err.code = ERROR_RTA_FAILED;
            err.message = MESSAGE_RTA_FAILED;
        }
        JsonRpcErrorResponse resp;
        resp.error = err;
        output.load(err);
        return true;
    }
    return false;
}

Status errorInvalidTransaction(const std::string &tx_data, Output &output)
{
    JsonRpcError err;
    err.code = ERROR_TRANSACTION_INVALID;
    err.message = MESSAGE_INVALID_TRANSACTION + " : " + tx_data;
    JsonRpcErrorResponse resp;
    resp.error = err;
    output.load(resp);
    return Status::Error;
}

void cleanPaySaleData(const std::string &payment_id, Context &ctx)
{
    ctx.global.remove(payment_id + CONTEXT_KEY_PAY);
    ctx.global.remove(payment_id + CONTEXT_KEY_SALE);
    ctx.global.remove(payment_id + CONTEXT_KEY_STATUS);
}



}
