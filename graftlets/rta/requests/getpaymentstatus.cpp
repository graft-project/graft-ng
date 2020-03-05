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

#include "getpaymentstatus.h"
#include "supernode/requests/common.h"
#include "supernode/requests/sale_status.h"
#include "supernode/requestdefines.h"
#include "lib/graft/requesttools.h"
#include "rta/supernode.h"
#include "rta/fullsupernodelist.h"
#include "lib/graft/common/utils.h"
#include "supernode/requests/broadcast.h"
#include "utils/cryptmsg.h" // one-to-many message cryptography
#include "updatepaymentstatus.h"


#undef MONERO_DEFAULT_LOG_CATEGORY
#define MONERO_DEFAULT_LOG_CATEGORY "supernode.getpaymentstatusrequest"


namespace graft::supernode::request {

Status getPaymentStatusRequest(const Router::vars_t &vars, const Input &input, Context &ctx, Output &output)
{
    PaymentStatusRequest req;
    if (!input.get(req)) {
        return errorInvalidParams(output);
    }

    // we have payment data
    if (!ctx.global.hasKey(req.PaymentID + CONTEXT_KEY_STATUS)) {
        return errorInvalidPaymentID(output);  // TODO: consider to change protocol to return 404?
    }

    MDEBUG("payment status found for payment id: " << req.PaymentID);
    PaymentStatus paymentStatus = ctx.global.get(req.PaymentID + CONTEXT_KEY_STATUS, PaymentStatus());
    PaymentStatusResponse resp;

    resp.PaymentID = paymentStatus.PaymentID;
    resp.Status = paymentStatus.Status;
    resp.Signature = paymentStatus.Signature;

    output.load(resp);
    return Status::Ok;
}

}


