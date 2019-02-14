
#include "supernode/requests/sale_status.h"
#include "supernode/requests/broadcast.h"
#include "supernode/requestdefines.h"
#include <misc_log_ex.h>

#undef MONERO_DEFAULT_LOG_CATEGORY
#define MONERO_DEFAULT_LOG_CATEGORY "supernode.salestatusrequest"

namespace graft::supernode::request {

using namespace std;

// json-rpc request from client
GRAFT_DEFINE_JSON_RPC_REQUEST(SaleStatusRequestJsonRpc, SaleStatusRequest);

// json-rpc response to client
GRAFT_DEFINE_JSON_RPC_RESPONSE_RESULT(SaleStatusResponseJsonRpc, SaleStatusResponse);


Status saleStatusHandler(const Router::vars_t& vars, const graft::Input& input,
                         graft::Context& ctx, graft::Output& output)
{
    SaleStatusRequestJsonRpc req;
    if (!input.get(req)) {
        return errorInvalidParams(output);
    }


    const SaleStatusRequest &in = req.params;
    MDEBUG("requested status for payment: " << in.PaymentID);
    int current_status = ctx.global.get(in.PaymentID + CONTEXT_KEY_STATUS, static_cast<int>(RTAStatus::None));
    if (in.PaymentID.empty() || current_status == 0)
    {
        MWARNING("no status for payment: " << in.PaymentID);
        return errorInvalidPaymentID(output);
    }

    MDEBUG("payment: " << in.PaymentID
           << ", status found: " << current_status);

    SaleStatusResponseJsonRpc out;
    out.result.Status = current_status;
    output.load(out);
    return Status::Ok;
}

Status updateSaleStatusHandler(const Router::vars_t& vars, const graft::Input& input,
                         graft::Context& ctx, graft::Output& output)
{

    BroadcastRequestJsonRpc req;
    if (!input.get(req)) {
        return errorInvalidParams(output);
    }


    JsonRpcError error;
    error.code = 0;

    UpdateSaleStatusBroadcast ussb;

    graft::Input innerInput;
    innerInput.load(req.params.data);

    if (!innerInput.getT<serializer::JSON_B64>(ussb)) {
        return errorInvalidParams(output);
    }

    SupernodePtr supernode = ctx.global.get(CONTEXT_KEY_SUPERNODE, SupernodePtr());


    MDEBUG("sale status update received for payment: " << ussb.PaymentID);

    if (!checkSaleStatusUpdateSignature(ussb.PaymentID, ussb.Status, ussb.address, ussb.signature, supernode)) {
        error.code = ERROR_RTA_SIGNATURE_FAILED;
        error.message = "status update: failed to validate signature for payment: " + ussb.PaymentID;
        LOG_ERROR(error.message);
        JsonRpcErrorResponse resp;
        resp.error = error;
        output.load(resp);
        return Status::Error;
    } else {
        // TODO: complete state chart for status transitions
        RTAStatus currentStatus = static_cast<RTAStatus>(ctx.global.get(ussb.PaymentID + CONTEXT_KEY_STATUS, int(RTAStatus::None)));
        if (!isFiniteRtaStatus(currentStatus)) {
            ctx.global.set(ussb.PaymentID + CONTEXT_KEY_STATUS, ussb.Status, RTA_TX_TTL);
            MDEBUG("sale status updated for payment: " << ussb.PaymentID << " to: " << ussb.Status);
        } else {
            MWARNING("status already in finite state for payment: " << ussb.PaymentID
                     << ", current status: " << int(currentStatus)
                     << ", wont update to: " << ussb.Status);
        }
    }

    BroadcastResponseToCryptonodeJsonRpc resp;
    resp.result.status = "OK";
    output.load(resp);

    return Status::Ok;
}



void registerSaleStatusRequest(graft::Router &router)
{
    Router::Handler3 h1(saleStatusHandler, nullptr, nullptr);
    router.addRoute("/sale_status", METHOD_POST, h1);
    Router::Handler3 h2(updateSaleStatusHandler, nullptr, nullptr);
    router.addRoute("/cryptonode/update_sale_status", METHOD_POST, h2);
}

string signSaleStatusUpdate(const string &payment_id, int status, const SupernodePtr &supernode)
{
    std::string msg = payment_id + ":" + to_string(status);
    crypto::signature sign;
    supernode->signMessage(msg, sign);
    return epee::string_tools::pod_to_hex(sign);
}


bool checkSaleStatusUpdateSignature(const string &payment_id, int status, const string &address, const string &signature,
                                    const SupernodePtr &supernode)
{
    crypto::signature sign;
    if (!epee::string_tools::hex_to_pod(signature, sign)) {
        LOG_ERROR("Error parsing signature: " << signature);
        return false;
    }

    std::string msg = payment_id + ":" + to_string(status);
    return supernode->verifySignature(msg, address, sign);
}

}

