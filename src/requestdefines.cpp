#include "requestdefines.h"
#include "jsonrpc.h"


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

}
