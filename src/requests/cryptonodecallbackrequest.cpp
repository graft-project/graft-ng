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

#include "cryptonodecallbackrequest.h"
#include "requestdefines.h"
#include "sendrawtxrequest.h"
#include <misc_log_ex.h>


/*
  /cryptonode_callback. this enpoint will be handling following methods:
  1. /cryptonode_callback/tx_to_sign - called by cryptonode. . called asyncronously, so result will be sent back later to
     cryptonode_address/json_rpc/, method = tx_to_sign_reply. in reply there will be either signed tx or rejected tx
  2. /cryptonode_



*/

namespace {
    static const char * PATH = "/cryptonode_callback";

}



namespace graft {

GRAFT_DEFINE_IO_STRUCT_INITED(TxToSignCallbackRequest,
                              (std::string, auth_supernode_addr, ""),
                              (std::string, hash, ""),
                              (std::string, signature, ""),
                              (std::string, tx_as_blob, "")
                              );

GRAFT_DEFINE_IO_STRUCT_INITED(TxToSignCallbackResponse,
                              (std::string, status, "")
                              );

GRAFT_DEFINE_JSON_RPC_REQUEST(TxToSignCallbackJsonRpcRequest, TxToSignCallbackRequest);
GRAFT_DEFINE_JSON_RPC_RESPONSE(TxToSignCallbackJsonRpcResponse, TxToSignCallbackResponse);
GRAFT_DEFINE_JSON_RPC_RESPONSE_RESULT(TxToSignCallbackJsonRpcResponseResult, TxToSignCallbackResponse);


/**
 * @brief cryptonodeCallbacksHandler - function handing json-rpc callbacks from cryptonode
 * @param vars
 * @param input
 * @param ctx
 * @param output
 * @return
 */
Status cryptonodeCallbacksHandler(const Router::vars_t& vars, const graft::Input& input,
                                 graft::Context& ctx, graft::Output& output)
{
    LOG_PRINT_L0(PATH << "called with payload: " << input.data());

    JsonRpcRequestHeader req_header = input.get<JsonRpcRequestHeader>();
    // TODO: probably make sense to implement the same 'macros-driven' framework like in monero to easily add handlers for each method
    if (req_header.method == "tx_to_sign") {
        TxToSignCallbackJsonRpcResponseResult resp;
        resp.id = req_header.id;
        resp.result.status = "OK";
        output.load(resp);
        //
        // here we need to asynchronously process "tx_to_sign" and callback cryptonode with "tx_to_sign_reply" method;
        // TODO: figure out how to run this async task with constraints of this framework,
        //
        return Status::Ok;
    } else {
        return Status::Error;
    }
}

void registerCryptonodeCallbacksRequest(graft::Router &router)
{
    Router::Handler3 h3(nullptr, cryptonodeCallbacksHandler, nullptr);

    router.addRoute(PATH, METHOD_POST, h3);
    LOG_PRINT_L2("route " << PATH << " registered");
}


}
