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
#include "requests/broadcast.h"
#include "rta/supernode.h"
#include <misc_log_ex.h>
#include <exception>

namespace {
    static const char * PATH_REQUEST =  "/cryptonode/authorize_rta_tx_request";
    static const char * PATH_RESPONSE = "/cryptonode/authorize_rta_tx_response";
    static const size_t RTA_VOTES_TO_REJECT =  1/*2*/; // TODO: 1 and 3 while testing
    static const size_t RTA_VOTES_TO_APPROVE = 1/*7*/;
}

namespace graft {



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

struct RtaAuthResult
{
    std::vector<SupernodeSignature> approved;
    std::vector<SupernodeSignature> rejected;
    bool alreadyApproved(const std::string &address)
    {
        return contains(approved, address);
    }

    bool alreadyRejected(const std::string &address)
    {
        return contains(rejected, address);
    }

private:
    bool contains(const std::vector<SupernodeSignature> &v, const std::string &address)
    {
        return std::find_if(v.begin(), v.end(), [&](const SupernodeSignature &item) {
            return item.address == address;
        }) != v.end();
    }
};

// TODO: this function duplicates PendingTransaction::putRtaSignatures
void putRtaSignaturesToTx(cryptonote::transaction &tx, const std::vector<SupernodeSignature> &signatures)
{
    std::vector<cryptonote::rta_signature> bin_signs;
    for (const auto &sign : signatures) {
        cryptonote::rta_signature bin_sign;
        epee::string_tools::hex_to_pod(sign.address, bin_sign.address);
        epee::string_tools::hex_to_pod(sign.signature, bin_sign.signature);
         bin_signs.push_back(bin_sign);
    }
    tx.put_rta_signatures(bin_signs);
}


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
    cryptonote::transaction tx;
    crypto::hash tx_hash, tx_prefix_hash;
    cryptonote::blobdata tx_blob;

    if (!epee::string_tools::parse_hexstr_to_binbuff(authReq.tx_hex, tx_blob)) {
        LOG_ERROR("Failed to parse hex tx: " << authReq.tx_hex);
        return errorInvalidTransaction(authReq.tx_hex, output);
    }

    if (!cryptonote::parse_and_validate_tx_from_blob(tx_blob, tx, tx_hash, tx_prefix_hash)) {
        LOG_ERROR("Failed to parse and validate tx from blob: " << authReq.tx_hex);
        return errorInvalidTransaction(authReq.tx_hex, output);
    }

    string tx_id_str = epee::string_tools::pod_to_hex(tx_hash);
    LOG_PRINT_L0(__FUNCTION__ << "incoming auth req with tx: " << tx_id_str);
    // check if we already processed this tx

    if (ctx.global.hasKey(tx_id_str + CONTEXT_KEY_TX_BY_TXID)) {
        LOG_ERROR("tx already processed: " << tx_id_str);
        return errorCustomError("tx already processed", ERROR_INVALID_PARAMS, output);
    }

    // check if we have a fee assigned by sender wallet
    uint64 amount = 0;
    if (!supernode->getAmountFromTx(tx, amount)) {
        LOG_ERROR("can't parse supernode fee for tx: " << tx_id_str);
        return errorCustomError("can't parse supernode fee for tx", ERROR_INVALID_PARAMS, output);
    }

//    // TODO: read payment id from transaction, map tx_id to payment_id
//    // string payment_id
//    if (!supernode->getPaymentIdFromTx(tx, payment_id) || payment_id.empty()) {
//        return errorInvalidPaymentID(output);
//    }
    // for the alpha we'll get payment id from /pay request

    MulticastRequestJsonRpc authResponseMulticast;
    authResponseMulticast.method = "multicast";
    authResponseMulticast.params.sender_address = supernode->walletAddress();
    authResponseMulticast.params.receiver_addresses = req.params.receiver_addresses;
    authResponseMulticast.params.callback_uri = PATH_RESPONSE;
    AuthorizeRtaTxResponse authResponse;
    authResponse.tx_id = tx_id_str;
    authResponse.result = static_cast<int>(amount > 0 ? RTAAuthResult::Approved : RTAAuthResult::Rejected);
    authResponse.signature.signature = signAuthResponse(authResponse, supernode);
    authResponse.signature.address   = supernode->walletAddress();

    // store tx
    ctx.global.set(authResponse.tx_id + CONTEXT_KEY_TX_BY_TXID, tx, RTA_TX_TTL);
    // TODO: remove it when payment id will be in tx.extra
    ctx.global.set(authResponse.tx_id + CONTEXT_KEY_PAYMENT_ID_BY_TXID, authReq.payment_id, RTA_TX_TTL);

    Output innerOut;
    innerOut.loadT<serializer::JSON_B64>(authResponse);
    authResponseMulticast.params.data = innerOut.data();
    output.load(authResponseMulticast);
    output.path = "/json_rpc/rta";
    LOG_PRINT_L0("transaction " << authResponse.tx_id << ", validate status: " << authResponse.result);
    LOG_PRINT_L0("calling supernode: " << output.path << " with payload: " << output.data());
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

    try {


    MulticastRequestJsonRpc req;
    LOG_PRINT_L0(__FUNCTION__ << " begin");

    if (!input.get(req)) { // can't parse request
        LOG_ERROR("failed to parse request: " + input.data());
        return errorCustomError(string("failed to parse request: ")  + input.data(), ERROR_INVALID_REQUEST, output);
    }

    // TODO: check if our address is listed in "receiver_addresses"
    AuthorizeRtaTxResponse rtaAuthResp;
    Input innerIn;

    innerIn.load(req.params.data);

    if (!innerIn.getT<serializer::JSON_B64>(rtaAuthResp)) {
        LOG_ERROR("error deserialize rta auth response");
        return errorInvalidParams(output);
    }

    SupernodePtr supernode = ctx.global.get(CONTEXT_KEY_SUPERNODE, SupernodePtr());
    LOG_PRINT_L0(__FUNCTION__ << " supernode loaded from context");


    RTAAuthResult result = static_cast<RTAAuthResult>(rtaAuthResp.result);
    // sanity check
    if (result != RTAAuthResult::Approved && result != RTAAuthResult::Rejected) {
        LOG_ERROR("Invalid rta auth result: " << rtaAuthResp.result);
        return errorInvalidParams(output);
    }

    LOG_PRINT_L0(__FUNCTION__ << " rta result: " << int(result));

    string ctx_payment_id_key = rtaAuthResp.tx_id + CONTEXT_KEY_PAYMENT_ID_BY_TXID;

    if (!ctx.global.hasKey(ctx_payment_id_key)) {
        LOG_ERROR("no payment_id for tx: " << rtaAuthResp.tx_id);
        return errorCustomError(string("unknown tx: ") + rtaAuthResp.tx_id, ERROR_INTERNAL_ERROR, output);
    }
    string payment_id = ctx.global.get(ctx_payment_id_key, std::string());

    LOG_PRINT_L0(__FUNCTION__ << " payment_id: " << payment_id);

    // validate signature
    bool signOk = validateAuthResponse(rtaAuthResp, supernode);
    if (!signOk) {
        string msg = "failed to validate signature for rta auth response";
        LOG_ERROR(msg);
        return errorCustomError(msg,
                                ERROR_RTA_SIGNATURE_FAILED,
                                output);
    }
    LOG_PRINT_L0(__FUNCTION__ << " rta_result signature validated ");

    // stop handling it if we already processed response

    RtaAuthResult authResult;
    string ctx_tx_to_auth_resp = rtaAuthResp.tx_id + CONTEXT_KEY_AUTH_RESULT_BY_TXID;
    if (ctx.global.hasKey(ctx_tx_to_auth_resp)) {
        authResult = ctx.global.get(ctx_tx_to_auth_resp, authResult);
    }

    if (authResult.alreadyApproved(rtaAuthResp.signature.address)
            || authResult.alreadyRejected(rtaAuthResp.signature.address)) {
        return errorCustomError(string("supernode: ") + rtaAuthResp.signature.address + " already processed",
                                ERROR_ADDRESS_INVALID, output);
    }

    if (result == RTAAuthResult::Approved) {
        authResult.approved.push_back(rtaAuthResp.signature);
    } else {
        authResult.rejected.push_back(rtaAuthResp.signature);
    }

    LOG_PRINT_L0(__FUNCTION__ << " rta result accepted from " << rtaAuthResp.signature.address);
    // store result in context
    ctx.global.set(ctx_tx_to_auth_resp, authResult, RTA_TX_TTL);
    LOG_PRINT_L0("approved votes: " << authResult.approved.size());
    LOG_PRINT_L0("rejected votes: " << authResult.rejected.size());

    LOG_PRINT_L0(__FUNCTION__ << " end");
    if (!ctx.global.hasKey(rtaAuthResp.tx_id + CONTEXT_KEY_TX_BY_TXID)) {
        string msg = string("rta auth response processed but no tx found for tx id: ") + rtaAuthResp.tx_id;
        LOG_ERROR(msg);
        return errorCustomError(msg, ERROR_INTERNAL_ERROR, output);
    }

    if (authResult.rejected.size() >= RTA_VOTES_TO_REJECT) {
        LOG_PRINT_L0("tx " << rtaAuthResp.tx_id << " rejected by auth sample, updating status");
        // tx rejected by auth sample, broadcast status;
        ctx.global[__FUNCTION__] = RtaAuthResponseHandlerState::StatusBroadcastReply;
        ctx.global.set(payment_id + CONTEXT_KEY_STATUS, static_cast<int> (RTAStatus::Fail), RTA_TX_TTL);
        buildBroadcastSaleStatusOutput(payment_id, static_cast<int> (RTAStatus::Fail), supernode, output);
        return Status::Forward;
    } else if (authResult.approved.size() >= RTA_VOTES_TO_APPROVE) {
        LOG_PRINT_L0("tx " << rtaAuthResp.tx_id << " approved by auth sample, pushing to tx pool");
        SendRawTxRequest req;

        // store tx_id in local context so we can use it when broadcasting status
        ctx.local[CONTEXT_TX_ID] = rtaAuthResp.tx_id;

        cryptonote::transaction tx = ctx.global.get(rtaAuthResp.tx_id + CONTEXT_KEY_TX_BY_TXID, cryptonote::transaction());
        putRtaSignaturesToTx(tx, authResult.approved);
        createSendRawTxRequest(tx, req);
        // call cryptonode

        output.load(req);
        output.path = "/sendrawtransaction";
        ctx.local[__FUNCTION__] = RtaAuthResponseHandlerState::TransactionPushReply;
        return Status::Forward;
    } else {
        LOG_PRINT_L0("not enough votes for approval/reject, keep waiting for other votes");
        AuthorizeRtaTxResponseJsonRpcResponse out;
        out.result.Result = STATUS_OK;
        output.load(out);
        return Status::Ok;
    }


    } catch (const std::exception &e) {
        LOG_ERROR("std::exception  catched: " << e.what());
        return errorInternalError(string("exception in cryptonode/authorize_rta_tx_response handler: ") +  e.what(),
                                         output);
    } catch (...) {
        LOG_ERROR("unhandled exception");
        return errorInternalError(string("unknown exception in cryptonode/authorize_rta_tx_response handler"),
                                         output);
    }
}


// handles "/sendrawtransaction" response
Status handleCryptonodeTxPushResponse(const Router::vars_t& vars, const graft::Input& input,
                               graft::Context& ctx, graft::Output& output)
{

    LOG_PRINT_L0(__FUNCTION__ << " begin");
    SendRawTxResponse resp;
    // check if we have tx_id in local context
    string tx_id = ctx.local[CONTEXT_TX_ID];
    if (tx_id.empty()) {
        LOG_ERROR("internal erorr, tx_id key not found in local context");
        abort();
    }
    // obtain payment id for given tx_id
    string payment_id = ctx.global.get(tx_id + CONTEXT_KEY_PAYMENT_ID_BY_TXID, std::string());
    if (payment_id.empty()) {
        LOG_ERROR("Internal error, payment id not found for tx id: " << tx_id);
    }
    SupernodePtr supernode = ctx.global.get(CONTEXT_KEY_SUPERNODE, SupernodePtr());
    // parse and handle "sendrawtransaction" response from cryptonode

    int status = static_cast<int>(RTAStatus::Success);
    if (!input.get(resp) || resp.status != "OK") {
        // broadcast with status "Fail"
        LOG_ERROR("failed to push tx to tx pool");
        // TODO: put txid to context, read it here and add to error message;
        status = static_cast<int>(RTAStatus::Fail);
    }
    LOG_PRINT_L0(__FUNCTION__ << " broadcasting status for payment id: " << payment_id << ", status : " << status);
    buildBroadcastSaleStatusOutput(payment_id, status, supernode, output);
    ctx.local[__FUNCTION__] = RtaAuthResponseHandlerState::StatusBroadcastReply;
    LOG_PRINT_L0(__FUNCTION__ << " end");
    return Status::Forward;
}

// handles status broadcast response, pass "ok" to caller (cryptonode)
Status handleStatusBroadcastResponse(const Router::vars_t& vars, const graft::Input& input,
                                     graft::Context& ctx, graft::Output& output)
{
    // TODO: check if cryptonode broadcasted status
    LOG_PRINT_L0(__FUNCTION__ << " begin");
    BroadcastResponseFromCryptonodeJsonRpc in;
    JsonRpcErrorResponse error;
    if (!input.get(in) || in.error.code != 0 || in.result.status != STATUS_OK) {
        return errorCustomError("Error broadcasting status", ERROR_INTERNAL_ERROR, output);
    }

    // most likely cryptonode doesn't really care what we reply here
    AuthorizeRtaTxResponseJsonRpcResponse outResponse;
    outResponse.result.Result = STATUS_OK;
    output.load(outResponse);
    LOG_PRINT_L0(__FUNCTION__ << " end");
    return Status::Ok;
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
    try {
        RtaAuthRequestHandlerState state = ctx.local.hasKey(__FUNCTION__) ? ctx.local[__FUNCTION__] : RtaAuthRequestHandlerState::ClientRequest;

        LOG_PRINT_L0(__FUNCTION__ << " state: " << (int) state);
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
    } catch (const std::exception &e) {
        LOG_ERROR(__FUNCTION__ << " exception thrown: " << e.what());
    } catch (...) {
        LOG_ERROR(__FUNCTION__ << " unknown exception thrown");
    }
    return errorInternalError("exception thrown", output);

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

    try {
        RtaAuthResponseHandlerState state = ctx.local.hasKey(__FUNCTION__) ? ctx.local[__FUNCTION__] : RtaAuthResponseHandlerState::RtaAuthReply;
        LOG_PRINT_L0(__FUNCTION__ << " state: " << int(state));

        switch (state) {
        // actually not a reply, just incoming multicast. same as "called by client" and client is cryptonode here
        case RtaAuthResponseHandlerState::RtaAuthReply:
            return handleRtaAuthResponseMulticast(vars, input, ctx, output);

        case RtaAuthResponseHandlerState::TransactionPushReply:
            return handleCryptonodeTxPushResponse(vars, input, ctx, output);

        case RtaAuthResponseHandlerState::StatusBroadcastReply:
            return handleStatusBroadcastResponse(vars, input, ctx, output);
        default:
            LOG_ERROR("Internal error, unexpected state: " << (int)state);
            abort();
        };
    } catch (const std::exception &e) {
        LOG_ERROR(__FUNCTION__ << " exception thrown: " << e.what());
    } catch (...) {
        LOG_ERROR(__FUNCTION__ << " unknown exception thrown");
    }
    return errorInternalError("exception thrown", output);


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
