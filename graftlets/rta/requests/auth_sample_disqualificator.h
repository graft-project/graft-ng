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

#pragma once

#include "lib/graft/router.h"
#include "lib/graft/inout.h"
#include "lib/graft/context.h"

#include <utils/cryptmsg.h>

#include <vector>

namespace graft::supernode::request
{

//The class is used for tests
class AuthSDisqualificator
{
public:
    //supernodes ids
    using Ids = std::vector<crypto::public_key>;

    //it should be called as earlier as possible, at the request when payment_id is known, to be prepared to respond to votes of disqualification requests while we are not ready yet to do disqualifications
    // returns Ok
    static graft::Status initDisqualify(graft::Context& ctx, const std::string& payment_id);

    //it should be called on payment timeout, when we know whom to disqualify
    //block_height is required to generate correct auth sample
    //ids - supernodes to be disqualified, they should be from the auth sample
    //it fills output with multicast request and returns Forward. call it even if ids empty, it will clear the context and return Ok
    static graft::Status startDisqualify(graft::Context& ctx, const std::string& payment_id, uint64_t block_height, const Ids& ids, graft::Output& output);
};

Status votesHandlerV2(const graft::Router::vars_t &vars, const Input &input, Context &ctx, Output &output);

} //namespace graft::supernode::request
