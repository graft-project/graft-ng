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

#include "paymentdatarequest.h"
#include "paymentdataresponse.h"
#include "common.h"

#include "supernode/requests/sale_status.h"

#include "supernode/requestdefines.h"
#include "lib/graft/requesttools.h"
#include "rta/supernode.h"
#include "rta/fullsupernodelist.h"
#include "supernode/requests/broadcast.h"
#include "utils/cryptmsg.h" // one-to-many message cryptography


#undef MONERO_DEFAULT_LOG_CATEGORY
#define MONERO_DEFAULT_LOG_CATEGORY "supernode.paymentdatarequest"

namespace graft::supernode::request {

Status handlePaymentDataRequestUnicast(const Router::vars_t &vars, const Input &input, Context &ctx, Output &output)
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
    PaymentDataRemoteRequest paymentDataRequest;

    if (!innerInput.getT<serializer::JSON_B64>(paymentDataRequest)) {
        MERROR("Failed parse PaymentDataRequest from : " << input.data());
        return sendOkResponseToCryptonode(output);
    }

    const std::string &payment_id = paymentDataRequest.PaymentID;
    MDEBUG("payment data unicast request received for payment id: " << payment_id);

    if (!ctx.global.hasKey(payment_id + CONTEXT_KEY_PAYMENT_DATA)) { // TODO: make sense to return this to caller's callback endpoint?
        MERROR("Payment data wasn't found for payment id: " << payment_id);
        return sendOkResponseToCryptonode(output);
    }

    SupernodePtr supernode = ctx.global.get(CONTEXT_KEY_SUPERNODE, SupernodePtr(nullptr));

    PaymentData paymentData = ctx.global.get(payment_id + CONTEXT_KEY_PAYMENT_DATA, PaymentData());
    std::string plain_msg = to_json_str(paymentData);
    std::string cypher_msg, cypher_msg_b64;
    std::vector<crypto::public_key> cypher_keys;
    cypher_keys.resize(1);
    epee::string_tools::hex_to_pod(req.sender_address, cypher_keys.at(0));
    graft::crypto_tools::encryptMessage(plain_msg, cypher_keys, cypher_msg);
    cypher_msg_b64 = graft::utils::base64_encode(cypher_msg);
    PaymentDataRemoteResponse resp;
    resp.PaymentID = payment_id;
    resp.EncryptedPaymentData = cypher_msg_b64;

    Output innerOut; innerOut.loadT<serializer::JSON_B64>(resp);

    BroadcastRequestJsonRpc callbackReq;
    callbackReq.params.sender_address = supernode->idKeyAsString();
    callbackReq.params.receiver_addresses.push_back(req.sender_address);
    callbackReq.params.callback_uri = "/core/payment_data_response";
    callbackReq.params.data = innerOut.data();
    callbackReq.method = "broadcast";
    output.path = "/json_rpc/rta";

    utils::signBroadcastMessage(callbackReq.params, supernode);

    output.load(callbackReq);
    MDEBUG("unicasting payment data response: " << output.data());

    return Status::Forward;
}


// handles unicast requests from remote cryptonode.
Status paymentDataRequest(const Router::vars_t& vars, const graft::Input& input,
                                 graft::Context& ctx, graft::Output& output)
{
    enum class State: int {
        ClientRequest = 0,    // requests comes from cryptonode
        CallbackToClient,     // unicast callback sent to cryptonode
        CallbackAcknowledge,  // cryptonode accepted callbacks,
    };

    State state = ctx.local.hasKey(__FUNCTION__) ? ctx.local[__FUNCTION__] : State::ClientRequest;

    switch (state) {
    case State::ClientRequest:
        ctx.local[__FUNCTION__] = State::CallbackToClient;
        return handlePaymentDataRequestUnicast(vars, input, ctx, output); // send Unicast callback
    case State::CallbackToClient:
        ctx.local[__FUNCTION__] = State::CallbackAcknowledge;
        return sendOkResponseToCryptonode(output);                       //  cryptonode accepted uncast callback,
    case State::CallbackAcknowledge:
        ctx.local[__FUNCTION__] = State::CallbackAcknowledge;
        return sendOkResponseToCryptonode(output);                       // send ok as reply to initial request
    }
    assert(false);
    return graft::Status::Ok;
}


}

