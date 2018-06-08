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
#include <misc_log_ex.h>

namespace graft {

Status authorizeRtaTxHandler(const Router::vars_t& vars, const graft::Input& input,
                                 graft::Context& ctx, graft::Output& output)
{
    // call from client
    if (!ctx.local.hasKey(__FUNCTION__)) {
        LOG_PRINT_L2("call from client, forwarding to cryptonode...");
        // just forward input to cryptonode
        AuthorizeRtaTxRequest req = input.get<AuthorizeRtaTxRequest>();
        output.load(req);
        ctx.local[__FUNCTION__] = true;
        return Status::Forward;
    } else {
        // response from cryptonode
        LOG_PRINT_L2("response from cryptonode : " << input.data());
        AuthorizeRtaTxResponse resp = input.get<AuthorizeRtaTxResponse>();
        if (resp.status == "OK") { // positive reply
            output.load(resp);
            return Status::Ok;
        } else {
            ErrorResponse ret;
            ret.code = ERROR_INTERNAL_ERROR;
            // ret.message = resp.reason;
            output.load(ret);
            return Status::Error;
        }
    }
}

void registerAuthorizeRtaTxRequest(graft::Router &router)
{
    Router::Handler3 h3(nullptr, authorizeRtaTxHandler, nullptr);
    const char * path = "/dapi/2.0/authorize_rta_tx";
    router.addRoute(path, METHOD_POST, h3);
    LOG_PRINT_L2("route " << path << " registered");
}


}
