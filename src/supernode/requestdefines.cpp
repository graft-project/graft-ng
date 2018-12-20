
#include "supernode/requestdefines.h"
#include "lib/graft/jsonrpc.h"
#include "lib/graft/context.h"
#include "rta/supernode.h"
#include "supernode/requests/broadcast.h"
#include "supernode/requests/sale_status.h"

#include <string_tools.h> // epee
#include <misc_log_ex.h>

namespace graft {

using namespace std;
using namespace graft::supernode::request;

Status errorInvalidPaymentID(Output& output)
{
    JsonRpcError err;
    err.code = ERROR_PAYMENT_ID_INVALID;
    err.message = MESSAGE_PAYMENT_ID_INVALID;
    JsonRpcErrorResponse resp;
    resp.error = err;
    output.load(resp);
    return Status::Error;
}

Status errorInvalidAmount(Output& output)
{
    JsonRpcError err;
    err.code = ERROR_AMOUNT_INVALID;
    err.message = MESSAGE_AMOUNT_INVALID;
    JsonRpcErrorResponse resp;
    resp.error = err;
    output.load(resp);
    return Status::Error;
}

Status errorInvalidAddress(Output& output)
{
    JsonRpcError err;
    err.code = ERROR_ADDRESS_INVALID;
    err.message = MESSAGE_ADDRESS_INVALID;
    JsonRpcErrorResponse resp;
    resp.error = err;
    output.load(resp);
    return Status::Error;
}

Status errorBuildAuthSample(Output& output)
{
    JsonRpcError err;
    err.code = ERROR_INTERNAL_ERROR;
    err.message = MESSAGE_RTA_CANT_BUILD_AUTH_SAMPLE;
    JsonRpcErrorResponse resp;
    resp.error = err;
    output.load(resp);
    return Status::Error;
}


bool errorFinishedPayment(int status, Output& output)
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

Status errorInvalidTransaction(const std::string& tx_data, Output& output)
{
    JsonRpcError err;
    err.code = ERROR_TRANSACTION_INVALID;
    err.message = MESSAGE_INVALID_TRANSACTION + " : " + tx_data;
    JsonRpcErrorResponse resp;
    resp.error = err;
    output.load(resp);
    return Status::Error;
}

Status errorInternalError(const std::string& message, Output& output)
{
    JsonRpcError err;
    err.code = ERROR_INTERNAL_ERROR;
    err.message = message;
    JsonRpcErrorResponse resp;
    resp.error = err;
    output.load(resp);
    return Status::Error;
}

Status errorCustomError(const std::string& message, int code, Output& output)
{
    JsonRpcError err;
    err.code = code;
    err.message = message;
    JsonRpcErrorResponse resp;
    resp.error = err;
    LOG_ERROR(__FUNCTION__ << " called with " << message << code);
    output.load(resp);
    return Status::Error;
}


void cleanPaySaleData(const std::string& payment_id, Context& ctx)
{
    ctx.global.remove(payment_id + CONTEXT_KEY_PAY);
    ctx.global.remove(payment_id + CONTEXT_KEY_SALE);
    ctx.global.remove(payment_id + CONTEXT_KEY_STATUS);
}

void buildBroadcastSaleStatusOutput(const std::string& payment_id, int status, const SupernodePtr& supernode, Output& output)
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
    output.path = "/json_rpc/rta";
    output.load(cryptonode_req);
}

GRAFT_DEFINE_IO_STRUCT_INITED(ResultResponse,
                        (int, Result, STATUS_OK)
                       );

GRAFT_DEFINE_JSON_RPC_RESPONSE_RESULT(ResultResponseJsonRpc, ResultResponse);

Status sendOkResponseToCryptonode(Output& output)
{
    ResultResponseJsonRpc res;
    output.load(res);
    return Status::Ok;
}

bool isFiniteRtaStatus(RTAStatus status)
{
    return !(status == RTAStatus::None || status == RTAStatus::InProgress
            || status == RTAStatus::Waiting);
}

} //namespace graft
