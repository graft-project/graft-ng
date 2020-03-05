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

#include "gettx.h"
#include "supernode/requests/common.h"
#include "sale.h"
#include "pay.h"

#include "supernode/requests/sale_status.h"

#include "supernode/requestdefines.h"
#include "lib/graft/requesttools.h"
#include "rta/supernode.h"
#include "rta/fullsupernodelist.h"
#include "lib/graft/common/utils.h"
#include "supernode/requests/broadcast.h"
#include "utils/cryptmsg.h" // one-to-many message cryptography


#undef MONERO_DEFAULT_LOG_CATEGORY
#define MONERO_DEFAULT_LOG_CATEGORY "supernode.gettx"

namespace graft::supernode::request::gettx {

Status handleClientTxRequest(const Router::vars_t& vars, const graft::Input& input,
                             graft::Context& ctx, graft::Output& output)
{

    GetTxRequest req;
    if (!input.get(req)) {
        return errorInvalidParams(output);
    }
    FullSupernodeListPtr fsl = ctx.global.get(CONTEXT_KEY_FULLSUPERNODELIST, FullSupernodeListPtr(nullptr));
    SupernodePtr supernode = ctx.global.get(CONTEXT_KEY_SUPERNODE, SupernodePtr());

    bool found = false;
    // we have payment data
    if (ctx.global.hasKey(req.PaymentID + CONTEXT_RTA_TX_REQ_TX_KEY)) {
        MDEBUG("tx found for payment id: " << req.PaymentID);
        cryptonote::transaction tx;
        crypto::secret_key txkey;

        if (!utils::getTxFromGlobalContext(ctx, tx, req.PaymentID + CONTEXT_RTA_TX_REQ_TX_KEY)) {
            std::string msg = "Internal error: failed to get tx for payment: " + req.PaymentID;
            MERROR(msg);
            return errorCustomError(msg, ERROR_RTA_FAILED, output);
        }

        if (!utils::getTxKeyFromContext(ctx, txkey, req.PaymentID + CONTEXT_RTA_TX_REQ_TX_KEY_KEY)) {
            std::string msg = "Internal error: failed to get txkey for payment: " + req.PaymentID;
            MERROR(msg);
            return errorCustomError(msg, ERROR_RTA_FAILED, output);
        }


        // get POS public key
        cryptonote::rta_header rta_hdr;
        if (!cryptonote::get_graft_rta_header_from_extra(tx, rta_hdr)) {
            std::string msg = "Internal error: failed to get rta_header for payment: " + req.PaymentID;
            MERROR(msg);
            return errorCustomError(msg, ERROR_RTA_FAILED, output);
        }

        // validate PoS signature;
        crypto::signature sign;
        if (!epee::string_tools::hex_to_pod(req.Signature, sign)) {
            std::string msg = "Internal error: failed to get rta_header for payment: " + req.PaymentID;
            MERROR(msg);
            return errorCustomError(msg, ERROR_RTA_FAILED, output);
        }

        if (!Supernode::verifySignature(req.PaymentID, rta_hdr.keys.at(cryptonote::rta_header::POS_KEY_INDEX), sign)) {
            std::string msg = "Failed to validate signature: " + req.PaymentID;
            MERROR(msg);
            return errorCustomError(msg, ERROR_RTA_FAILED, output);
        }

        std::vector<crypto::public_key> recepient_keys;
        recepient_keys.push_back(rta_hdr.keys.at(cryptonote::rta_header::POS_KEY_INDEX));
        GetTxResponse resp;
        utils::encryptTxToHex(tx, recepient_keys, resp.TxBlob);
        utils::encryptTxKeyToHex(txkey, recepient_keys, resp.TxKeyBlob);

        output.load(resp);
        return Status::Ok;
    } else {
        // TODO: implement requesting transaction from remote supernode, but it's not really necessary as rta tx always sent to PosProxy
        std::string msg = "Internal error: no tx for payment id: " + req.PaymentID;
        MERROR(msg);
        return errorCustomError(msg, ERROR_RTA_FAILED, output);
    }
}

Status handleCryptonodeReply(const Router::vars_t& vars, const graft::Input& input,
                                graft::Context& ctx, graft::Output& output)
{
    // check cryptonode reply
    MDEBUG(__FUNCTION__ << " begin");
    BroadcastResponseFromCryptonodeJsonRpc resp;

    JsonRpcErrorResponse error;
    if (!input.get(resp) || resp.error.code != 0 || resp.result.status != STATUS_OK) {
        return errorCustomError("Error unicasting request", ERROR_INTERNAL_ERROR, output);
    }
    output.reset();
    output.resp_code = 202;
    return Status::Ok;
}

} // namespace graft::supernode::request::getpaymentdata


namespace graft::supernode::request {

Status getTxRequest(const Router::vars_t &vars, const Input &input, Context &ctx, Output &output)
{
    enum class HandlerState : int
    {
        ClientRequest = 0, // incoming wallet request
        CryptonodeReply    // cryptonode forward reply
    };


    HandlerState state = ctx.local.hasKey(__FUNCTION__) ? ctx.local[__FUNCTION__] : HandlerState::ClientRequest;
    // state machine to perform a) multicast to cryptonode
    switch (state) {
    // client requested "/get_payment_data"
    case HandlerState::ClientRequest:
        // ctx.local[__FUNCTION__] = GetPaymentDataHandlerState::CryptonodeReply;
        return gettx::handleClientTxRequest(vars, input, ctx, output);
    case HandlerState::CryptonodeReply:
        // handle "unicast" response from cryptonode, check it's status;
        MDEBUG("GetPaymentData unicast response from cryptonode: " << input.data());
        MDEBUG("status: " << (int)ctx.local.getLastStatus());
        return gettx::handleCryptonodeReply(vars, input, ctx, output);
    default:
        LOG_ERROR("Internal error: unhandled state");
        abort();
    };
}

}


