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

#include "posrejectpaymentrequest.h"
#include "common.h"

#include "supernode/requests/broadcast.h"

#include "supernode/requestdefines.h"
#include "lib/graft/requesttools.h"
#include "rta/supernode.h"
#include "rta/fullsupernodelist.h"


#include <cryptonote_basic/cryptonote_basic.h>
#include <cryptonote_basic/cryptonote_format_utils.h>
#include <utils/cryptmsg.h> // one-to-many message cryptography

#include <string>

#undef MONERO_DEFAULT_LOG_CATEGORY
#define MONERO_DEFAULT_LOG_CATEGORY "supernode.posrejectpayment"

namespace graft::supernode::request {

enum class PosRejectPaymentHandlerState : int
{
    ClientRequest = 0,
    MulticastReply
};



Status handleClientPosRejectPaymentRequest(const Router::vars_t& vars, const graft::Input& input, graft::Context& ctx, graft::Output& output)
{
    EncryptedPaymentStatus req;

    if (!input.get(req)) {
        MERROR("Failed to deseialize request");
        return errorInvalidParams(output);
    }

    // get auth sample by payment id:
    if (!ctx.global.hasKey(req.PaymentID + CONTEXT_KEY_PAYMENT_DATA)) {
        std::string msg = std::string("Unknown payment id: ") + req.PaymentID;
        return errorCustomError(msg, ERROR_INVALID_PARAMS, output);
    }

    if (req.PaymentStatusBlob.empty()) {
        MERROR("Empty PaymentRejectBlob");
        return errorInvalidParams(output);
    }

    PaymentData paymentData = ctx.global.get(req.PaymentID + CONTEXT_KEY_PAYMENT_DATA, PaymentData());
    SupernodePtr supernode = ctx.global.get(CONTEXT_KEY_SUPERNODE, SupernodePtr());

    BroadcastRequest bcast;
    for (const auto &key : paymentData.AuthSampleKeys) {
        bcast.receiver_addresses.push_back(key.Id);
        // debug
#if 0
        bcast.receiver_addresses.push_back(supernode->idKeyAsString());
#endif
    }

    Output innerOut;
    innerOut.loadT<serializer::JSON_B64>(req);

    bcast.sender_address = supernode->idKeyAsString();
    bcast.data = innerOut.data();
    bcast.callback_uri = "/core/update_payment_status_encrypted";

    if (!utils::signBroadcastMessage(bcast, supernode)) {
        std::string msg = std::string("Failed to sign broadcast message: ") + req.PaymentID;
        return errorInternalError(msg, output);
    }

    BroadcastRequestJsonRpc cryptonode_req;
    cryptonode_req.method = "broadcast";
    cryptonode_req.params = std::move(bcast);

    output.load(cryptonode_req);
    output.path = "/json_rpc/rta";
    MDEBUG("multicasting: " << output.data());
    return Status::Forward;
}

Status handlePosRejectMulticastReply(const Router::vars_t& vars, const graft::Input& input, graft::Context& ctx, graft::Output& output)
{
    MDEBUG(__FUNCTION__ << " begin");
    BroadcastResponseFromCryptonodeJsonRpc resp;

    JsonRpcErrorResponse error;
    if(!input.get(resp) || resp.error.code || resp.result.status != STATUS_OK)
        return errorCustomError("Error multicasting request", ERROR_INTERNAL_ERROR, output);

    output.reset();
    output.resp_code = 202;
    return Status::Ok;
}

/*!
 * \brief handleApprovePaymentRequest - handles /dapi/v3.0/approve_payment Wallet request
 * \param vars
 * \param input
 * \param ctx
 * \param output
 * \return
 */
Status handlePosRejectPaymentRequest(const Router::vars_t& vars, const graft::Input& input, graft::Context& ctx, graft::Output& output)
{
    PosRejectPaymentHandlerState state =
      ctx.local.hasKey(__FUNCTION__) ? ctx.local[__FUNCTION__] : PosRejectPaymentHandlerState::ClientRequest;

    switch(state) // state machine to perform a) multicast to cryptonode
    {
        // client requested "/approve_payment"
        case PosRejectPaymentHandlerState::ClientRequest:
            ctx.local[__FUNCTION__] = PosRejectPaymentHandlerState::MulticastReply;
            // call cryptonode's "/rta/multicast" to send pay data to auth sample
            // "handleClientPayRequest" returns Forward;
            return handleClientPosRejectPaymentRequest(vars, input, ctx, output);

        case PosRejectPaymentHandlerState::MulticastReply:
            // handle "multicast" response from cryptonode, check it's status, send
            // "pay status" with broadcast to cryptonode
            MDEBUG("PosReject response from cryptonode: " << input.data());
            MDEBUG("status: " << (int)ctx.local.getLastStatus());
            return handlePosRejectMulticastReply(vars, input, ctx, output);

        default:
            LOG_ERROR("Internal error: unhandled state");
            abort();
    };
}

} // namespace graft::supernode::request

