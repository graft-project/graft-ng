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
#include "supernode/requests/broadcast.h"
#include "lib/graft/jsonrpc.h"
#include <misc_log_ex.h>

#undef MONERO_DEFAULT_LOG_CATEGORY
#define MONERO_DEFAULT_LOG_CATEGORY "supernode.updatepaymentstatusrequest"

namespace graft::supernode::request {


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
    UpdatePaymentStatusRequest updateStatusRequest;  //

    if (!innerInput.getT<serializer::JSON_B64>(updateStatusRequest)) {
        MERROR("Failed to parse UpdatePaymentStatus: " << innerInput.data());
        return sendOkResponseToCryptonode(output);
    }

    const std::string &payment_id = updateStatusRequest.PaymentID;
    MDEBUG("update payment status received from broadcast: " << payment_id << ", New status: " << updateStatusRequest.Status);

    ctx.global.set(payment_id + CONTEXT_KEY_STATUS, updateStatusRequest.Status, SALE_TTL);

    return sendOkResponseToCryptonode(output);
}



}

