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

#include "pay.h"
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
#define MONERO_DEFAULT_LOG_CATEGORY "supernode.payrequest"

namespace graft::supernode::request {

const std::chrono::seconds PAY_TTL = std::chrono::seconds(60);

enum class PayHandlerState : int
{
    ClientRequest = 0,
    PayMulticastReply
};



Status handleClientPayRequest(const Router::vars_t& vars, const graft::Input& input, graft::Context& ctx, graft::Output& output)
{
    PayRequest req;

    if (!input.get(req))
        return errorInvalidParams(output);

    if (req.TxBlob.empty() || req.TxKey.empty())
        return errorInvalidParams(output);

    // we are going to
    // - multicast pay over auth sample + two proxy

    SupernodePtr supernode = ctx.global.get(CONTEXT_KEY_SUPERNODE, SupernodePtr());

    // decrypt transaction
    cryptonote::transaction tx;
    if (!utils::decryptTxFromHex(req.TxBlob, supernode, tx)) {
        return errorCustomError("Failed to decrypt tx", ERROR_RTA_FAILED, output);
    }

    // check if we have rta_header in tx.extra;
    cryptonote::rta_header rta_hdr;
    if (!cryptonote::get_graft_rta_header_from_extra(tx, rta_hdr)) {
        return errorCustomError("Failed to get rta_header from tx: " +
                                epee::string_tools::pod_to_hex(cryptonote::get_transaction_hash(tx)),
                                ERROR_RTA_FAILED, output);
    }

    BroadcastRequest bcast;
    for (const auto &key : rta_hdr.keys) {
        bcast.receiver_addresses.push_back(epee::string_tools::pod_to_hex(key));
    }

    Output innerOut;
    innerOut.loadT<serializer::JSON_B64>(req);

    bcast.sender_address = supernode->idKeyAsString();
    bcast.data = innerOut.data();
    bcast.callback_uri = "/dapi/v2.0/core/authorize_rta_tx_request";

    if(!utils::signBroadcastMessage(bcast, supernode))
        return errorInternalError("Failed to sign broadcast message", output);

    BroadcastRequestJsonRpc cryptonode_req;
    cryptonode_req.method = "broadcast";
    cryptonode_req.params = std::move(bcast);

    output.load(cryptonode_req);
    output.path = "/json_rpc/rta";
    MDEBUG("multicasting: " << output.data());
    return Status::Forward;
}

Status handlePayMulticastReply(const Router::vars_t& vars, const graft::Input& input, graft::Context& ctx, graft::Output& output)
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
 * \brief handlePayRequest - handles /dapi/v3.0/pay Wallet request
 * \param vars
 * \param input
 * \param ctx
 * \param output
 * \return
 */
Status handlePayRequest(const Router::vars_t& vars, const graft::Input& input, graft::Context& ctx, graft::Output& output)
{
    PayHandlerState state =
      ctx.local.hasKey(__FUNCTION__) ? ctx.local[__FUNCTION__] : PayHandlerState::ClientRequest;

    switch(state) // state machine to perform a) multicast to cryptonode
    {
        // client requested "/pay"
        case PayHandlerState::ClientRequest:
            ctx.local[__FUNCTION__] = PayHandlerState::PayMulticastReply;
            // call cryptonode's "/rta/multicast" to send pay data to auth sample
            // "handleClientPayRequest" returns Forward;
            return handleClientPayRequest(vars, input, ctx, output);

        case PayHandlerState::PayMulticastReply:
            // handle "multicast" response from cryptonode, check it's status, send
            // "pay status" with broadcast to cryptonode
            MDEBUG("PayMulticast response from cryptonode: " << input.data());
            MDEBUG("status: " << (int)ctx.local.getLastStatus());
            return handlePayMulticastReply(vars, input, ctx, output);

        default:
            LOG_ERROR("Internal error: unhandled state");
            abort();
    };
}

} // namespace graft::supernode::request

