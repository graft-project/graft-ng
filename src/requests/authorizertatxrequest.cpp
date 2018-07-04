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
#include <random>
#include <functional>

namespace {

bool randomBool() {
    static auto gen = std::bind(std::uniform_int_distribution<>(0,1),std::default_random_engine());
    return gen();
}
}


namespace graft {

static const char * PATH = "/authorize_rta_tx"; // root router already has prefix "/dapi/v2.0" so we don't need a prefix here


Status authorizeRtaTxHandler(const Router::vars_t& vars, const graft::Input& input,
                                 graft::Context& ctx, graft::Output& output)
{
    // call from client
    LOG_PRINT_L2("authorize rta tx");
    AuthorizeRtaTxRequest req;
    bool parsed = input.get<AuthorizeRtaTxRequest>(req);

    AuthorizeRtaTxResponse resp;

    if (!parsed) {
        resp.Status = ERROR_INVALID_REQUEST;
        output.load(resp);
        return Status::Error;
    }

    resp.tx_id = req.tx_info.id;
    resp.supernode_addr = req.supernode_addr;

    if (randomBool()) {
        // return "signed"
        resp.Status = STATUS_OK;
        resp.signature = "1234567890";
    } else {
        // return "failed"
        resp.Status = ERROR_RTA_FAILED;
        resp.message  = "Failed to validate tx";
    }

    output.load(resp);
    return Status::Ok;
}

void registerAuthorizeRtaTxRequest(graft::Router &router)
{
    Router::Handler3 h3(nullptr, authorizeRtaTxHandler, nullptr);
    router.addRoute(PATH, METHOD_POST, h3);
    LOG_PRINT_L2("route " << PATH << " registered");
}


}
