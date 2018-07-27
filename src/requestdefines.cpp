#include "requestdefines.h"
#include "jsonrpc.h"
#include "context.h"
#include "rta/supernode.h"
#include "requests/broadcast.h"
#include "requests/salestatusrequest.h"

#include <string_tools.h> // epee

namespace graft {

using namespace std;

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

Status errorInternalError(const std::string &message, Output &output)
{
    JsonRpcError err;
    err.code = ERROR_INTERNAL_ERROR;
    err.message = message;
    JsonRpcErrorResponse resp;
    resp.error = err;
    output.load(resp);
    return Status::Error;
}

Status errorCustomError(const std::string &message, int code, Output &output)
{
    JsonRpcError err;
    err.code = code;
    err.message = message;
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

void buildBroadcastSaleStatusOutput(const std::string &payment_id, int status, const SupernodePtr &supernode, Output &output)
{
    UpdateSaleStatusBroadcast ussb;
    ussb.address = supernode->walletAddress();
    ussb.Status =  status;
    ussb.PaymentID = payment_id;

    // sign message
    std::string msg = payment_id + ":" + to_string(ussb.Status);
    crypto::signature sign;
    supernode->signMessage(msg, sign);
    ussb.signature = epee::string_tools::pod_to_hex(sign);

    Output innerOut;
    innerOut.loadT<serializer::JSON_B64>(ussb);

    // send payload
    BroadcastRequestJsonRpc cryptonode_req;
    cryptonode_req.method = "broadcast";
    cryptonode_req.params.callback_uri = "/cryptonode/update_sale_status";
    cryptonode_req.params.data = innerOut.data();
    output.load(cryptonode_req);
    output.uri = "/json_rpc/rta";
    output.load(cryptonode_req);
}



}
