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

#include "paymentdataresponse.h"
#include "common.h"
#include "sale.h"
#include "supernode/requestdefines.h"
#include "lib/graft/requesttools.h"
#include "rta/supernode.h"
#include "rta/fullsupernodelist.h"
#include "supernode/requests/broadcast.h"
#include "utils/cryptmsg.h" // one-to-many message cryptography


#undef MONERO_DEFAULT_LOG_CATEGORY
#define MONERO_DEFAULT_LOG_CATEGORY "supernode.paymentdataresponse"

namespace graft::supernode::request {


Status paymentDataResponse(const Router::vars_t &vars, const Input &input, Context &ctx, Output &output)
{
    BroadcastRequestJsonRpc reqjrpc;

    if (!input.get(reqjrpc)) {
        // cryptonode isn't supposed to handle any errors, it's job just to deliver a message
        MERROR("Failed to parse request");
        return sendOkResponseToCryptonode(output);
    }

    BroadcastRequest &req = reqjrpc.params;

    if (!utils::verifyBroadcastMessage(req, req.sender_address)) {
        MERROR("Failed to verify signature for message: " << input.data());
        return sendOkResponseToCryptonode(output);
    }

    graft::Input innerInput;
    innerInput.load(req.data);
    PaymentDataRemoteResponse paymentDataResponse;

    if (!innerInput.getT<serializer::JSON_B64>(paymentDataResponse)) {
        MERROR("Failed parse PaymentDataRemoteResponse from : " << input.data());
        return sendOkResponseToCryptonode(output);
    }

    MDEBUG("paymend data response for payment id: " << paymentDataResponse.PaymentID);
    SupernodePtr supernode = ctx.global.get(CONTEXT_KEY_SUPERNODE, SupernodePtr(nullptr));

    std::string encryptedMessage = graft::utils::base64_decode(paymentDataResponse.EncryptedPaymentData);
    std::string paymentDataJson;
    if (!graft::crypto_tools::decryptMessage(encryptedMessage, supernode->secretKey(), paymentDataJson)) {
        MERROR("Failed to decrypt payment data for payment id: " << paymentDataResponse.PaymentID);
        return sendOkResponseToCryptonode(output);
    }
    PaymentData paymentData;
    Input in; in.load(paymentDataJson);

    if (!in.get(paymentData)) {
        MERROR("Failed to de-serialize payment data from json: " << paymentDataJson);
        return sendOkResponseToCryptonode(output);
    }

    ctx.global.set(paymentDataResponse.PaymentID + CONTEXT_KEY_PAYMENT_DATA, paymentData, SALE_TTL);

    return sendOkResponseToCryptonode(output);
}


}

