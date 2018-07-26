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
    static const char * PATH_REQUEST =  "/authorize_rta_tx_request";
    static const char * PATH_RESPONSE = "/authorize_rta_tx_response";
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

enum class HandlerState : int {
    ClientRequest = 0,
    CryptonodeReply,
};


string signAuthResponse(const AuthorizeRtaTxResponse &arg, const SupernodePtr &supernode)
{
    crypto::signature sign;
    supernode->signMessage(arg.tx_id + ":" + to_string(arg.result), sign);
    return epee::string_tools::pod_to_hex(sign);
}

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

Status handleTxAuthRequest(const Router::vars_t& vars, const graft::Input& input,
                            graft::Context& ctx, graft::Output& output)
{
    LOG_PRINT_L0(PATH_REQUEST << "called with payload: " << input.data());
    MulticastRequestJsonRpc req;
    if (!input.get(req)) { // can't parse request
        JsonRpcError error;
        error.code = ERROR_INVALID_REQUEST;
        error.message = "Failed to parse request";
        JsonRpcErrorResponse errorResponse;
        errorResponse.error = error;
        errorResponse.jsonrpc = "2.0";
        output.load(errorResponse);
        return Status::Error;
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
        // TODO: specific error ?
        return errorInvalidParams(output);
    }

    MulticastRequestJsonRpc authResponseMulticast;
    authResponseMulticast.method = "multicast";
    authResponseMulticast.params.sender_address = supernode->walletAddress();
    authResponseMulticast.params.receiver_addresses = req.params.receiver_addresses;
    authResponseMulticast.params.callback_uri = string("/cryptonode") + PATH_RESPONSE;
    AuthorizeRtaTxResponse authResponse;
    authResponse.tx_id = epee::string_tools::pod_to_hex(tx_hash);
    authResponse.result = static_cast<int>(amount > 0 ? RTAAuthResult::Invalid : RTAAuthResult::Rejected);
    authResponse.signature.signature = signAuthResponse(authResponse, supernode);
    authResponse.signature.address   = supernode->walletAddress();

    Output innerOut;
    innerOut.loadT<serializer::JSON_B64>(authResponse);
    authResponseMulticast.params.data = innerOut.data();
    output.load(authResponseMulticast);
    output.uri = ctx.global.getConfig()->cryptonode_rpc_address + "/json_rpc/rta";
    return Status::Forward;
}

Status handleCryptonodeMulticastResponse(const Router::vars_t& vars, const graft::Input& input,
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

    HandlerState state = ctx.local.hasKey(__FUNCTION__) ? ctx.local[__FUNCTION__] : HandlerState::ClientRequest;
    switch (state) {
    case HandlerState::ClientRequest:
        LOG_PRINT_L0("called by client, payload: " << input.data());
        ctx.local[__FUNCTION__] = HandlerState::CryptonodeReply;
        return handleTxAuthRequest(vars, input, ctx, output);
    case HandlerState::CryptonodeReply:
        return handleCryptonodeMulticastResponse(vars, input, ctx, output);
    default: // internal error
        return errorInternalError(string("authorize_rta_tx_request: unhandled state: ") + to_string(int(state)),
                                  output);
    };


}

Status authorizeRtaTxResponseHandler(const Router::vars_t& vars, const graft::Input& input,
                                 graft::Context& ctx, graft::Output& output)
{

    LOG_PRINT_L0(PATH_RESPONSE << "called with payload: " << input.data());

    MulticastRequestJsonRpc req;


    if (!input.get(req)) { // can't parse request
        JsonRpcError error;
        error.code = ERROR_INVALID_REQUEST;
        error.message = "Failed to parse request";
        JsonRpcErrorResponse errorResponse;
        errorResponse.error = error;
        errorResponse.jsonrpc = "2.0";
        output.load(errorResponse);
        return Status::Error;
    }

    // TODO: handle tx auth response callback
    // send normal response

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
