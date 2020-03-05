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

#pragma once

#include "lib/graft/router.h"
#include "lib/graft/jsonrpc.h"
#include "supernode/requestdefines.h"

namespace graft::supernode::request {

// SupernodeInfoRequest request payload
GRAFT_DEFINE_IO_STRUCT_INITED(SupernodeInfoRequest,
    (std::vector<std::string>, input, std::vector<std::string>())
);

GRAFT_DEFINE_IO_STRUCT_INITED(SupernodeInfo,
    (std::string, Address, std::string()),
    (std::string, PublicId, std::string()),
    (uint64, StakeAmount, 0),
    (uint64, StakeFirstValidBlock, 0),
    (uint64, StakeExpiringBlock, 0),
    (bool, IsStakeValid, 0),
    (unsigned int, BlockchainBasedListTier, 0),
    (unsigned int, AuthSampleBlockchainBasedListTier, 0),
    (bool, IsAvailableForAuthSample, false),
    (uint64, LastUpdateAge, 0)
);


GRAFT_DEFINE_IO_STRUCT_INITED(SupernodeInfoResponse,
    (std::vector<SupernodeInfo>, output,  std::vector<SupernodeInfo>())
);

Status handleSupernodeInfoRequest(const Router::vars_t& vars, const graft::Input& input,
                         graft::Context& ctx, graft::Output& output);

} // namespace graft::supernode::request

