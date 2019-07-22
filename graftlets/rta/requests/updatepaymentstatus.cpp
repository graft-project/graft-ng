// Copyright (c) 2019, The Graft Project
//
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without modification, are
// permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice, this list of
//    conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright notice, this list
//    of conditions and the following disclaimer in the documentation and/or other
//    materials provided with the distribution.
//
// 3. Neither the name of the copyright holder nor the names of its contributors may be
//    used to endorse or promote products derived from this software without specific
//    prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
// EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
// THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
// STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
// THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.


#include "updatepaymentstatus.h"
#include "sale.h"
#include "supernode/requestdefines.h"
#include "common.h"
#include "posrejectpaymentrequest.h"
#include "supernode/requests/broadcast.h"
#include "lib/graft/jsonrpc.h"
#include "rta/supernode.h"
#include "rta/fullsupernodelist.h"

#include <string_tools.h> // graftnoded's contrib/epee/include
#include <misc_log_ex.h>  // graftnoded's log macros

#include <cryptonote_basic/cryptonote_basic.h>
#include <cryptonote_basic/cryptonote_format_utils.h>


#undef MONERO_DEFAULT_LOG_CATEGORY
#define MONERO_DEFAULT_LOG_CATEGORY "supernode.updatepaymentstatusrequest"

namespace graft::supernode::request {



bool validateStatusUpdateAuthSample(graft::Context &ctx, const PaymentStatus &req)
{
    cryptonote::transaction tx;
    if (!utils::getTxFromGlobalContext(ctx, tx, req.PaymentID + CONTEXT_RTA_TX_REQ_TX_KEY)) {
        MERROR("Failed to find transaction for payment id: " << req.PaymentID);
        return false;
    }

    cryptonote::rta_header rta_hdr;
    if (!cryptonote::get_graft_rta_header_from_extra(tx, rta_hdr))  {
        MERROR("Failed to read rta_header from tx, payment id: " << req.PaymentID);
        return false;
    }

    crypto::signature sig;
    if (!epee::string_tools::hex_to_pod(req.Signature, sig)) {
        MERROR("Failed to parse signature from: " << req.Signature);
        return false;
    }


    crypto::hash h = paymentStatusGetHash(req);
    for (const auto &key : rta_hdr.keys) {
        if (crypto::check_signature(h, key, sig)) {
            return true;
        }
    }

    return false;

}

bool validateStatusUpdateBroadcast(graft::Context &ctx, const PaymentStatus &req)
{
    FullSupernodeListPtr fsl = ctx.global.get(CONTEXT_KEY_FULLSUPERNODELIST, FullSupernodeListPtr());
    FullSupernodeList::supernode_array auth_sample;
    uint64_t unused;
    if (!fsl->buildAuthSample(req.PaymentBlock, req.PaymentID, auth_sample, unused)) {
        MERROR("Failed to build auth sample for, payment_id: " << req.PaymentID << ", block: " << req.PaymentBlock);
        return false;
    }
    crypto::hash h = paymentStatusGetHash(req);

    crypto::signature sig;

    if (!epee::string_tools::hex_to_pod(req.Signature, sig)) {
        MERROR("Failed to parse signature for payment id: " << graft::to_json_str(req));
        return false;
    }

    for (const auto & sn : auth_sample) {
        if (crypto::check_signature(h, sn->idKey(), sig))
            return  true;
    }

    return false;

}


Status handleUpdatePaymentStatusRequest(const Router::vars_t& vars, const graft::Input& input,
                        graft::Context& ctx, graft::Output& output)
{
    BroadcastRequestJsonRpc reqjrpc;
    if (!input.get(reqjrpc)) {
        MERROR("Failed to parse request: " << input.data());
        return sendOkResponseToCryptonode(output);

    }

    // JSON-RPC envelop, we need to extract actual request;
    BroadcastRequest &req = reqjrpc.params;

    if (!utils::verifyBroadcastMessage(req, req.sender_address)) {
        MERROR("Signature verification failed: " << input.data());
        return sendOkResponseToCryptonode(output);
    }

    graft::Input innerInput;
    innerInput.load(req.data);
    PaymentStatus paymentStatus;  //

    if (!innerInput.getT<serializer::JSON_B64>(paymentStatus)) {
        MERROR("Failed to parse UpdatePaymentStatus: " << innerInput.data());
        return sendOkResponseToCryptonode(output);
    }

    MDEBUG("received payment status update: " << graft::to_json_str(paymentStatus));

    const std::string &payment_id = paymentStatus.PaymentID;

    // check if it signed by auth sample or pos or pos proxy or wallet proxy
    if (!validateStatusUpdateBroadcast(ctx, paymentStatus)) {
        MERROR("Status update signed by invalid node ignoring, paymentID: " << paymentStatus.PaymentID
               << ", signature: " << paymentStatus.Signature);
        return sendOkResponseToCryptonode(output);
    }

    MDEBUG("update payment status received from broadcast: " << payment_id << ", New status: " << paymentStatus.Status);

    ctx.global.set(payment_id + CONTEXT_KEY_STATUS, paymentStatus, SALE_TTL);

    return sendOkResponseToCryptonode(output);
}




// process status update received from auth sample
Status processStatusUpdateEx(const Router::vars_t& vars, const graft::Input& input,
                        graft::Context& ctx, graft::Output& output)
{




    BroadcastRequestJsonRpc reqjrpc;
    if (!input.get(reqjrpc)) {
        MERROR("Failed to parse request: " << input.data());
        return sendOkResponseToCryptonode(output);
    }

    // JSON-RPC envelop, we need to extract actual request;
    BroadcastRequest &req = reqjrpc.params;

    if (!utils::verifyBroadcastMessage(req, req.sender_address)) {
        MERROR("Signature verification failed: " << input.data());
        return sendOkResponseToCryptonode(output);
    }

    graft::Input innerInput;
    innerInput.load(req.data);
    EncryptedPaymentStatus encryptedPaymentStatus;


    if (!innerInput.getT<serializer::JSON_B64>(encryptedPaymentStatus)) {
        MERROR("Failed to parse EncryptedPaymentStatus: " << innerInput.data());
        return sendOkResponseToCryptonode(output);
    }

    MDEBUG("received payment status update: " << graft::to_json_str(encryptedPaymentStatus));

    const std::string &payment_id = encryptedPaymentStatus.PaymentID;

    // decrypt it
    SupernodePtr supernode = ctx.global.get(CONTEXT_KEY_SUPERNODE, SupernodePtr());

    PaymentStatus paymentStatus;

    if (!paymentStatusDecrypt(encryptedPaymentStatus, supernode->secretKey(), paymentStatus)) {
        MERROR("Failed to decrypt encrypted payment status, paymentID: " << encryptedPaymentStatus.PaymentID);
        return sendOkResponseToCryptonode(output);
    }

    // check if it signed by auth sample or pos or pos proxy or wallet proxy
    if (!validateStatusUpdateAuthSample(ctx, paymentStatus)) {
        MERROR("Status update signed by invalid node ignoring, paymentID: " << paymentStatus.PaymentID
               << ", signature: " << paymentStatus.Signature);
        return sendOkResponseToCryptonode(output);
    }

    if (ctx.global.hasKey(payment_id + CONTEXT_KEY_STATUS)) {
        PaymentStatus ourPaymentStatus = ctx.global.get(payment_id + CONTEXT_KEY_STATUS, PaymentStatus());
        if (ourPaymentStatus.Status == paymentStatus.Status)
            return sendOkResponseToCryptonode(output);
    }

    MDEBUG("update payment status received from auth sample: " << payment_id << ", New status: " << paymentStatus.Status);

    ctx.global.set(payment_id + CONTEXT_KEY_STATUS, paymentStatus, SALE_TTL);

    PaymentStatus newStatus = paymentStatus;


    MDEBUG("broadcasting status for payment id: " << newStatus.PaymentID << ", status : " << newStatus.Status);

    if (!paymentStatusSign(supernode, paymentStatus)) {
        MERROR("Failed to sign update payment status: " << paymentStatus.PaymentID);
        return Status::Ok;
    }

    utils::buildBroadcastOutput(paymentStatus, supernode, std::vector<std::string>(), "json_rpc/rta", "/core/update_payment_status", output);

    return Status::Forward;
}

Status handleUpdatePaymentStatusRequestEx(const Router::vars_t& vars, const graft::Input& input, graft::Context& ctx, graft::Output& output)
{
    enum class UpdateStatusExHandlerState : int
    {
        ClientRequest = 0,
        ClientRequestStored,
        BroadcastReply
    };


    UpdateStatusExHandlerState state =
      ctx.local.hasKey(__FUNCTION__) ? ctx.local[__FUNCTION__] : UpdateStatusExHandlerState::ClientRequest;

    switch(state) // state machine to perform a) multicast to cryptonode
    {
        // client requested "/pay"
        case UpdateStatusExHandlerState::ClientRequest:
            ctx.local[__FUNCTION__] = UpdateStatusExHandlerState::ClientRequestStored;
            ctx.local["request"] = input;
            return Status::Again;

        case UpdateStatusExHandlerState::ClientRequestStored:
            MDEBUG("status: " << (int)ctx.local.getLastStatus());
            ctx.local[__FUNCTION__] = UpdateStatusExHandlerState::BroadcastReply;
            return processStatusUpdateEx(vars, input, ctx, output);

        case UpdateStatusExHandlerState::BroadcastReply:
            return Status::Ok;

        default:
            LOG_ERROR("Internal error: unhandled state");
            abort();
    };
}


}

