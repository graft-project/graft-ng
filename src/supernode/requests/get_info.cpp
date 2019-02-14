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

#include "supernode/requests/get_info.h"
#include "supernode/requestdefines.h"
#include <misc_log_ex.h>

#undef MONERO_DEFAULT_LOG_CATEGORY
#define MONERO_DEFAULT_LOG_CATEGORY "supernode.getinforequest"

namespace graft::supernode::request {

GRAFT_DEFINE_JSON_RPC_RESPONSE(GetInfoResponseJsonRpc, GetInfoResponse);


Status getInfoHandler(const Router::vars_t& vars, const graft::Input& input,
                                 graft::Context& ctx, graft::Output& output)
{

    // Flow:
    // 1 -> call from client : we write request to the output and return "Forward" - it will be forwarded to cryptonode
    //      to check if it call from client - we use ctx.local, which is map created only for this client
    // 2 -> response from cryptonode: we parse input, handle response from cryptonode, compose output to client and return Ok
    // -> this will be forwarded to client

    //1. call from client
    //  1. validate input (in not valid, reply error)
    //  2. prepare request to cryptonode, including
    //     2.1 URI (path),
    //     2.2 HTTP method (nice to have, but we can start with POST only
    //     2.3 HTTP body (normally JSON RPC but could be some arbitrary JSON which is not valid JSON RPC)
    //  3. return Forward, which tells framework to forward request to cryptonode
    LOG_PRINT_L2(__FUNCTION__);
    if (!ctx.local.hasKey(__FUNCTION__)) {
        LOG_PRINT_L2("call from client, forwarding to cryptonode...");
        JsonRpcRequestHeader req;
        req.method = "get_info";
        output.load(req);
        output.path = "/json_rpc";
        // alternatively, it could NOT be done like this:
        // output.uri = ctx.global.getConfig()->cryptonode_rpc_address + "/json_rpc";
        ctx.local[__FUNCTION__] = true;
        return Status::Forward;
    } else {
    // 2. response from cryptonode
        // Suggested flow ;
        //  1. Check if any network errors here (we need to introduce interface for this)
        //  2. If no network errors, read http status code (we need to introduce interface for this)
        //  3. if http status code is ok (200) read body and parse it
        //  4. handle parsed response and prepare reply to the client

        LOG_PRINT_L2("response from cryptonode (input) : " << input.data());
        LOG_PRINT_L2("response from cryptonode (output) : " << output.data());

        Status status = ctx.local.getLastStatus();
        std::string error = ctx.local.getLastError();

        LOG_PRINT_L2("status: " << (int)status);
        LOG_PRINT_L2("error: " << error);
        LOG_PRINT_L2("input.http code: " << input.resp_code);
        LOG_PRINT_L2("input.http status msg: " << input.resp_status_msg);

        GetInfoResponseJsonRpc resp;
        bool parsed = input.get<GetInfoResponseJsonRpc>(resp);

        if (!parsed || resp.error.code != 0) {
            LOG_PRINT_L2("error response");
            ErrorResponse ret;
            ret.code = ERROR_INTERNAL_ERROR;
            ret.message = resp.error.message;
            output.load(ret);
            return Status::Error;
        } else {
            LOG_PRINT_L2("normal reply");
            GetInfoResponse ret;
            ret = resp.result;
            output.load(ret);
            return Status::Ok;
        }
    }
}

void registerGetInfoRequest(graft::Router& router)
{
    Router::Handler3 h3(nullptr, getInfoHandler, nullptr);
    const char * path = "/cryptonode/getinfo";
    router.addRoute(path, METHOD_GET, h3);
    LOG_PRINT_L2("route " << path << " registered");
}

}

