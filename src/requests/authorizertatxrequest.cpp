// Copyright (c) 2018, The Graft Project
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

#include "authorizertatxrequest.h"
#include "requestdefines.h"
#include "jsonrpc.h"
#include "sendrawtxrequest.h"
#include "requests/multicast.h"
#include "rta/supernode.h"
#include <misc_log_ex.h>

namespace {
    static const char * PATH_REQUEST =  "/cryptonode/authorize_rta_tx_request";
    static const char * PATH_RESPONSE = "/cryptonode/authorize_rta_tx_response";
    static const size_t RTA_VOTES_TO_REJECT =  2;
    static const size_t RTA_VOTES_TO_APPROVE = 7;
    static const std::chrono::seconds RTA_TX_TTL = std::chrono::seconds(60);
}

namespace graft {

struct RtaAuthResult
{
    size_t rejects_count = 0;
    size_t approves_count = 0;
};

GRAFT_DEFINE_IO_STRUCT_INITED(SupernodeSignature,
                              (std::string, address, std::string()),
                              (std::string, signature, std::string())
                              );


GRAFT_DEFINE_IO_STRUCT_INITED(AuthorizeRtaTxRequestResponse,
                        (int, Result, STATUS_OK)
                       );


GRAFT_DEFINE_IO_STRUCT_INITED(AuthorizeRtaTxResponse,
                       (std::string, tx_id, std::string()),
                       (int, result, int(RTAAuthResult::Invalid)),
                       (SupernodeSignature, signature, SupernodeSignature())
                       );

GRAFT_DEFINE_IO_STRUCT_INITED(AuthorizeRtaTxResponseResponse,
                       (int, Result, STATUS_OK)
                       );

GRAFT_DEFINE_JSON_RPC_RESPONSE_RESULT(AuthorizeRtaTxRequestJsonRpcResponse, AuthorizeRtaTxRequestResponse);
GRAFT_DEFINE_JSON_RPC_RESPONSE_RESULT(AuthorizeRtaTxResponseJsonRpcResponse, AuthorizeRtaTxResponseResponse);

enum class RtaAuthRequestHandlerState : int {
    ClientRequest = 0, // incoming request from client
    CryptonodeReply   // cryptonode replied to milticast
};

enum class RtaAuthResponseHandlerState : int {
    // Multicast call from cryptonode auth rta auth response
    RtaAuthReply = 0,
    // we pushed tx to tx pool, next is to broadcast status,
    TransactionPushReply,
    // Status broadcast reply
    StatusBroadcastReply
};



/*!
 * \brief signAuthResponse - signs RTA auth result
 * \param arg
 * \param supernode
 * \return
 */
string signAuthResponse(const AuthorizeRtaTxResponse &arg, const SupernodePtr &supernode)
{
    crypto::signature sign;
    supernode->signMessage(arg.tx_id + ":" + to_string(arg.result), sign);
    return epee::string_tools::pod_to_hex(sign);
}

/*!
 * \brief validateAuthResponse - validates (checks) RTA auth result signed by supernode
 * \param arg
 * \param supernode
 * \return
 */
bool validateAuthResponse(const AuthorizeRtaTxResponse &arg, const SupernodePtr &supernode)
{
    crypto::signature sign;
    if (!epee::string_tools::hex_to_pod(arg.signature.signature, sign)) {
        LOG_ERROR("Error parsing signature: " << arg.signature.signature);
        return false;
    }

    std::string msg = arg.tx_id + ":" + to_string(arg.result);
    return supernode->verifySignature(msg, arg.signature.address, sign);
}

/*!
 * \brief handleTxAuthRequest - handles RTA auth request multicasted over auth sample. Handler either approves or rejects transaction
 * \param vars
 * \param input
 * \param ctx
 * \param output
 * \return
 */

Status handleTxAuthRequest(const Router::vars_t& vars, const graft::Input& input,
                            graft::Context& ctx, graft::Output& output)
{
    LOG_PRINT_L0(PATH_REQUEST << "called with payload: " << input.data());
    MulticastRequestJsonRpc req;
    if (!input.get(req)) { // can't parse request
        return errorCustomError(string("failed to parse request: ")  + input.data(), ERROR_INVALID_REQUEST, output);
    }

    SupernodePtr supernode = ctx.global.get(CONTEXT_KEY_SUPERNODE, SupernodePtr());

    // check if we handling own request
    if (req.params.sender_address == supernode->walletAddress()) {
        MulticastResponseToCryptonodeJsonRpc resp;
        resp.result.status = "OK";
        output.load(resp);
        return Status::Ok;
    }

    AuthorizeRtaTxRequest authReq;
    Input innerInput;
    innerInput.load(req.params.data);
    LOG_PRINT_L0("input loaded");

    if (!innerInput.getT<serializer::JSON_B64>(authReq)) {
        return errorInvalidParams(output);
    }

    // de-serialize transaction
    LOG_PRINT_L0("transaction to validate: " << authReq.tx_blob);


    cryptonote::transaction tx;
    crypto::hash tx_hash, tx_prefix_hash;
    if (!cryptonote::parse_and_validate_tx_from_blob(authReq.tx_blob, tx, tx_hash, tx_prefix_hash)) {
        return errorInvalidTransaction(authReq.tx_blob, output);
    }

    // TODO check if we have a fee assigned by sender wallet
    // Functionality already implemented in feature/rta-check-ptx branch, Utils::lookup_account_outputs_ringct function
    // TODO: merge everything to "develop" branch

    uint64 amount = 0;
    if (!supernode->getAmountFromTx(tx, amount)) {
        // TODO: return specific error ?
        return errorInvalidParams(output);
    }
    // read payment id from transaction, map tx_id to payment_id
    string payment_id;
    if (!supernode->getPaymentIdFromTx(tx, payment_id) || payment_id.empty()) {
        return errorInvalidPaymentID(output);
    }
    ctx.global.set(epee::string_tools::pod_to_hex(tx_hash) + CONTEXT_KEY_TXID, payment_id, RTA_TX_TTL);

    MulticastRequestJsonRpc authResponseMulticast;
    authResponseMulticast.method = "multicast";
    authResponseMulticast.params.sender_address = supernode->walletAddress();
    authResponseMulticast.params.receiver_addresses = req.params.receiver_addresses;
    authResponseMulticast.params.callback_uri = string("/cryptonode") + PATH_RESPONSE;
    AuthorizeRtaTxResponse authResponse;
    authResponse.tx_id = epee::string_tools::pod_to_hex(tx_hash);
    authResponse.result = static_cast<int>(amount > 0 ? RTAAuthResult::Approved : RTAAuthResult::Rejected);
    authResponse.signature.signature = signAuthResponse(authResponse, supernode);
    authResponse.signature.address   = supernode->walletAddress();

    Output innerOut;
    innerOut.loadT<serializer::JSON_B64>(authResponse);
    authResponseMulticast.params.data = innerOut.data();
    output.load(authResponseMulticast);
    output.path = "/json_rpc/rta";
    return Status::Forward;
}

/*!
 * \brief handleCryptonodeMulticastResponse - handles multicast response from cryptonode. Here we only interested in error checking,
 *                                            there's nothing in response except "ok"
 *
 * \param vars
 * \param input
 * \param ctx
 * \param output
 * \return
 */
Status handleCryptonodeMulticastStatus(const Router::vars_t& vars, const graft::Input& input,
                            graft::Context& ctx, graft::Output& output)
{

    // check cryptonode reply
    MulticastResponseFromCryptonodeJsonRpc resp;

    JsonRpcErrorResponse error;
    if (!input.get(resp) || resp.error.code != 0 || resp.result.status != STATUS_OK) {
        return  errorInternalError("Error nulticasting request", output);
    }

    AuthorizeRtaTxRequestJsonRpcResponse out;
    out.result.Result = STATUS_OK;
    output.load(out);

    return Status::Ok;
}

/*!
 * \brief handleRtaAuthResponseMulticast - handles cryptonode/authorize_rta_tx_response call
 * \param vars
 * \param input
 * \param ctx
 * \param output
 * \return
 */
Status handleRtaAuthResponseMulticast(const Router::vars_t& vars, const graft::Input& input,
                            graft::Context& ctx, graft::Output& output)
{
    MulticastRequestJsonRpc req;

    if (!input.get(req)) { // can't parse request
        return errorCustomError(string("failed to parse request: ")  + input.data(), ERROR_INVALID_REQUEST, output);
    }

    // TODO: check if our address is listed in "receiver_addresses"
    AuthorizeRtaTxResponse rtaAuthResp;
    Input innerIn;

    innerIn.load(req.params.data);

    if (!innerIn.getT<serializer::JSON_B64>(rtaAuthResp)) {
        return errorInvalidParams(output);
    }
    SupernodePtr supernode = ctx.global.get(CONTEXT_KEY_SUPERNODE, SupernodePtr());

    RTAAuthResult result = static_cast<RTAAuthResult>(rtaAuthResp.result);
    // sanity check
    if (result != RTAAuthResult::Approved && result != RTAAuthResult::Rejected) {
        LOG_ERROR("Invalid rta auth result: " << rtaAuthResp.result);
        return errorInvalidParams(output);
    }

    // validate signature
    bool signOk = validateAuthResponse(rtaAuthResp, supernode);
    if (!signOk) {
        return errorCustomError(string("failed to validate signature for rta auth response"),
                                ERROR_RTA_SIGNATURE_FAILED,
                                output);
    }

    RtaAuthResult authResult;
    string ctx_tx_key = rtaAuthResp.tx_id + CONTEXT_KEY_TXID;
    if (ctx.global.hasKey(ctx_tx_key)) {
        authResult = ctx.global.get(ctx_tx_key, authResult);
    }

    if (result == RTAAuthResult::Approved) {
        ++authResult.approves_count;
    } else {
        ++authResult.rejects_count;
    }

    if (authResult.rejects_count >= RTA_VOTES_TO_REJECT) {
        // ctx.global.set(ctx_tx_key, )
    } else if (authResult.approves_count >= RTA_VOTES_TO_APPROVE) {
        // send tx to pool
        // broadcast auth approved
    }

}


/*!
 * \brief authorizeRtaTxRequestHandler - called by supernode as multicast request. handles rta authorization request
 * \param vars
 * \param input
 * \param ctx
 * \param output
 * \return
 */
Status authorizeRtaTxRequestHandler(const Router::vars_t& vars, const graft::Input& input,
                                 graft::Context& ctx, graft::Output& output)
{

    RtaAuthRequestHandlerState state = ctx.local.hasKey(__FUNCTION__) ? ctx.local[__FUNCTION__] : RtaAuthRequestHandlerState::ClientRequest;
    switch (state) {
    case RtaAuthRequestHandlerState::ClientRequest:
        LOG_PRINT_L0("called by client, payload: " << input.data());
        ctx.local[__FUNCTION__] = RtaAuthRequestHandlerState::CryptonodeReply;
        return handleTxAuthRequest(vars, input, ctx, output);
    case RtaAuthRequestHandlerState::CryptonodeReply:
        return handleCryptonodeMulticastStatus(vars, input, ctx, output);
    default: // internal error
        return errorInternalError(string("authorize_rta_tx_request: unhandled state: ") + to_string(int(state)),
                                  output);
    };


}

/*!
 * \brief authorizeRtaTxResponseHandler - handles supernode's RTA auth response multicasted over auth sample
 * \param vars
 * \param input
 * \param ctx
 * \param output
 * \return
 */
Status authorizeRtaTxResponseHandler(const Router::vars_t& vars, const graft::Input& input,
                                 graft::Context& ctx, graft::Output& output)
{

    LOG_PRINT_L0(PATH_RESPONSE << "called with payload: " << input.data());
    RtaAuthResponseHandlerState state = ctx.local.hasKey(__FUNCTION__) ? ctx.local[__FUNCTION__] : RtaAuthResponseHandlerState::RtaAuthReply;

    switch (state) {
    case RtaAuthResponseHandlerState::RtaAuthReply:

        break;


    }





}


void registerAuthorizeRtaTxRequests(graft::Router &router)
{
    Router::Handler3 request_handler(nullptr, authorizeRtaTxRequestHandler, nullptr);
    Router::Handler3 response_handler(nullptr, authorizeRtaTxResponseHandler, nullptr);
    router.addRoute(PATH_REQUEST, METHOD_POST, request_handler);
    LOG_PRINT_L1("route " << PATH_REQUEST << " registered");
    router.addRoute(PATH_RESPONSE, METHOD_POST, response_handler);
    LOG_PRINT_L1("route " << PATH_RESPONSE << " registered");

}

}
