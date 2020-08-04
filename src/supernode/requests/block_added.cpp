// Copyright (c) 2020, The Graft Project
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

#include "supernode/requests/block_added.h"
#include "supernode/requestdefines.h"
#include "rta/fullsupernodelist.h"
#include "rta/supernode.h"

#include <misc_log_ex.h>
#include <boost/shared_ptr.hpp>

#undef MONERO_DEFAULT_LOG_CATEGORY
#define MONERO_DEFAULT_LOG_CATEGORY "supernode.block_added"

namespace {
    static const char* PATH = "/block_added";
}

namespace graft::supernode::request {

namespace
{

Status storeRequestAndReplyOk(const Router::vars_t& vars, const graft::Input& input, graft::Context& ctx, graft::Output& output)
{
    // TODO
    return sendAgainResponseToCryptonode(output);
}

Status handleBlockAdded(const Router::vars_t& vars, const graft::Input& input, graft::Context& ctx, graft::Output& output)
{
    // TODO
    return Status::Ok;
}



// main 'entry point' handler
Status blockAddedHandler (const Router::vars_t& vars, const graft::Input& input, graft::Context& ctx, graft::Output& output)
{
    enum class BlockAddedHandlerState : int
    {
        RequestReceived = 0,  // cryptonode notified 'block_added'
        RequestStored,        // supernode stored block info for further processing and sent "ok" to cryptonode
        CheckpointVoteSent,   // supernode sent vote to cryptonode
        CheckpointVoteAcknowledged // cryptonote acknowledged vote
        
    };


    BlockAddedHandlerState state = ctx.local.hasKey(__FUNCTION__) ? ctx.local[__FUNCTION__] : BlockAddedHandlerState::RequestReceived;
    // state machine 
    switch (state) {
    // client requested "/get_payment_data"
    case BlockAddedHandlerState::RequestReceived:
        ctx.local[__FUNCTION__] = BlockAddedHandlerState::RequestStored;
        return storeRequestAndReplyOk(vars, input, ctx, output);
    case BlockAddedHandlerState::RequestStored:
        return handleBlockAdded(vars, input, ctx, output);
    case BlockAddedHandlerState::CheckpointVoteSent:
        return Status::Ok;
    default:
        LOG_ERROR("Internal error: unhandled state");
        abort();
    };
  
    MINFO(PATH << " called with payload: " << input.data());
  
    return Status::Ok;
}

}

void registerBlockchainBasedListRequest(graft::Router &router)
{
    Router::Handler3 h3(nullptr, blockAddedHandler, nullptr);

    router.addRoute(PATH, METHOD_POST, h3);

    LOG_PRINT_L0("route " << PATH << " registered");
}

}
