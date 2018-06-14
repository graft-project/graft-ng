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

#include "sendsupernodeannouncerequest.h"
#include "requestdefines.h"
#include "sendrawtxrequest.h"
#include <misc_log_ex.h>



namespace {
    static const char * PATH = "/send_supernode_announce";

}

/*
{
    "json_rpc" : "2.0",
    "method" : "SendSupernodeAnnounce",
    "id" : "<unique_id>",
    "params": {

    }
}

*/


namespace graft {

GRAFT_DEFINE_IO_STRUCT_INITED(SupernodeAnnounce,
                              (uint64_t, timestamp, 0),
                              (std::string, address, std::string()),
                              (uint64_t, stake_amount, 0),
                              (std::string, key_images, std::string())
                              );


GRAFT_DEFINE_IO_STRUCT_INITED(SendSupernodeAnnounceResponse,
                              (int, Status, 0)
                              );

GRAFT_DEFINE_JSON_RPC_REQUEST(SendSupernodeAnnounceJsonRpcRequest, SupernodeAnnounce);
GRAFT_DEFINE_JSON_RPC_RESPONSE_RESULT(SendSupernodeAnnounceJsonRpcResponse, SendSupernodeAnnounceResponse);


/**
 * @brief
 * @param vars
 * @param input
 * @param ctx
 * @param output
 * @return
 */
Status sendSupernodeAnnounceHandler(const Router::vars_t& vars, const graft::Input& input,
                                 graft::Context& ctx, graft::Output& output)
{
    LOG_PRINT_L2(PATH << "called with payload: " << input.data());

    SendSupernodeAnnounceJsonRpcRequest req;

    if (!input.get(req)) { // can't parse request
        JsonRpcError error;
        error.code = ERROR_INVALID_REQUEST;
        error.message = "Failed to parse request";
        JsonRpcErrorResponse errorResponse;
        errorResponse.error = error;
        errorResponse.json = "2.0";
        output.load(errorResponse);
        return Status::Error;
    }
    // TODO: handle tx auth response callback
    // send normal response

    SendSupernodeAnnounceJsonRpcResponse response;
    response.id = req.id;
    response.json ="2.0";
    response.result.Status = STATUS_OK;
    output.load(response);
    return Status::Ok;
}

void registerSendSupernodeAnnounceRequest(graft::Router &router)
{
    Router::Handler3 h3(nullptr, sendSupernodeAnnounceHandler, nullptr);

    router.addRoute(PATH, METHOD_POST, h3);
    LOG_PRINT_L2("route " << PATH << " registered");
}


}
