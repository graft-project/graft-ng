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
#include "supernode/requests/common.h"
#include "rta/fullsupernodelist.h"
#include "rta/supernode.h"

#include <misc_log_ex.h>
#include <graft_rta_config.h>

#include <boost/shared_ptr.hpp>

#undef MONERO_DEFAULT_LOG_CATEGORY
#define MONERO_DEFAULT_LOG_CATEGORY "supernode.block_added"

namespace {
    static const char* PATH = "/block_added";
}

namespace graft::supernode::request {

GRAFT_DEFINE_IO_STRUCT_INITED(CheckpointVoteRequest,
                                (std::string, public_id, std::string()),
                                (uint64_t, block_height, 0),
                                (std::string, block_hash, std::string()),
                                (std::string, signature, std::string())
                              );

GRAFT_DEFINE_JSON_RPC_REQUEST(CheckpointVoteRequestJsonRpc, CheckpointVoteRequest);

namespace
{

enum class BlockAddedHandlerState : int
{
    RequestReceived = 0,  // cryptonode notified 'block_added'
    RequestStored,        // supernode stored block info for further processing and sent "ok" to cryptonode
    CheckpointVoteSent,   // supernode sent vote to cryptonode
    CheckpointVoteAcknowledged // cryptonote acknowledged vote
};


Status storeRequestAndReplyOk(const Router::vars_t& vars, const graft::Input& input, graft::Context& ctx, graft::Output& output)
{

    // store request in local ctx.
    BlockAddedJsonRpcResponse response;
    response.result.status = 0;
    output.load(response);
    BlockAddedJsonRpcRequest request;
    if (!input.get(request)) {
        MERROR("Failed to parse input: " << input.data());
        return Status::Ok;
    }
    ctx.local["request"] = request;
    
    return sendAgainResponseToCryptonode(output);
}

Status handleBlockAdded(const Router::vars_t& vars, const graft::Input& input, graft::Context& ctx, graft::Output& output)
{
    
    BlockAddedJsonRpcRequest request = ctx.local["request"];
    // check if we need to checkpoint the block
    if (request.params.height % /*config::graft::CHECKPOINT_INTERVAL*/ 1 != 0) {
        MDEBUG("Received block " << request.params.height << " will not be checkpointed, skipping");
        return Status::Ok;
    }
    // check if we already checkpointed this block
    std::string last_checkpointed_blockhash = ctx.global.get(CONTEXT_KEY_LAST_CHECKPOINTED_BLOCKHASH, "");
    if (request.params.block_hash == last_checkpointed_blockhash) {
        MDEBUG("Received block " << request.params.height << " already checkpointed, skipping");
        return Status::Ok;
    }
    
    crypto::hash seed_hash, block_hash;
    if (!epee::string_tools::hex_to_pod(request.params.seed_hash, seed_hash)) {
        MERROR("Failed to parse seed hash: " << request.params.seed_hash);
        return Status::Ok;
    }
    if (!epee::string_tools::hex_to_pod(request.params.block_hash, block_hash)) {
        MERROR("Failed to parse block hash: " << request.params.block_hash);
        return Status::Ok;
    }
    
    // build checkpoint sample
    FullSupernodeListPtr fsl = ctx.global.get(CONTEXT_KEY_FULLSUPERNODELIST, FullSupernodeListPtr());
    FullSupernodeList::supernode_array sample;
    uint64_t actual_sample_height {0};
    
    if (!fsl->buildCheckpointingSample(request.params.height, seed_hash, sample, actual_sample_height)) {
        MERROR("Failed to build checkpoint sample for height: " << request.params.height);
        return Status::Ok;
    }
    
    MDEBUG("Built checkpointing sample for height: " << actual_sample_height);
    {
        std::ostringstream oss;
        for (const auto & sn : sample) {
            oss << sn->idKeyAsString() << std::endl;
        }
        MDEBUG("Checkpointing sample: " << oss.str());
    }
        
    SupernodePtr supernode = ctx.global.get(CONTEXT_KEY_SUPERNODE, SupernodePtr());
    bool in_sample = std::find_if(sample.begin(), sample.end(), [&supernode](const auto &s) {
                return supernode->idKey() == s->idKey();
            }) != sample.end();
            
    in_sample = true;  // XXX: remove after debugging
    
    if (in_sample) {
        MDEBUG("Signing the block: " << block_hash);
        
        CheckpointVoteRequestJsonRpc checkpoint_vote;
        crypto::signature signature;
        supernode->signHash(block_hash, signature);
        
        checkpoint_vote.params.public_id = supernode->idKeyAsString();
        checkpoint_vote.params.block_height = request.params.height;
        checkpoint_vote.params.block_hash = request.params.block_hash;
        checkpoint_vote.params.signature = epee::string_tools::pod_to_hex(signature);
        checkpoint_vote.method = "checkpoint_vote";
        
        output.load(checkpoint_vote);
        output.path = "/json_rpc/rta";
        MDEBUG("Sending to cryptonode: " << output.data());
        
        ctx.local[RTA_HANDLE_BLOCK_STATE_KEY] = BlockAddedHandlerState::CheckpointVoteSent;
        return Status::Forward;
    }
    
    return Status::Ok;
}



// main 'entry point' handler
Status blockAddedHandler (const Router::vars_t& vars, const graft::Input& input, graft::Context& ctx, graft::Output& output)
{

    
    BlockAddedHandlerState state = ctx.local.hasKey(RTA_HANDLE_BLOCK_STATE_KEY) ? ctx.local[RTA_HANDLE_BLOCK_STATE_KEY] : BlockAddedHandlerState::RequestReceived;
    // state machine 
    MDEBUG(__FUNCTION__ << ", state: "  << int(state) << ", input: " << input.data());
    switch (state) {
    // client requested "/get_payment_data"
    case BlockAddedHandlerState::RequestReceived:
        ctx.local[RTA_HANDLE_BLOCK_STATE_KEY] = BlockAddedHandlerState::RequestStored;
        return storeRequestAndReplyOk(vars, input, ctx, output);
    case BlockAddedHandlerState::RequestStored:
        return handleBlockAdded(vars, input, ctx, output);
    case BlockAddedHandlerState::CheckpointVoteSent: 
        // no need to handle cryptonode response, as it normally one-way notification rather than request-response
        return Status::Ok;
    default:
        LOG_ERROR("Internal error: unhandled state");
        abort();
    }
  
    MINFO(PATH << " called with payload: " << input.data());
  
    return Status::Ok;
}

} // namespace

void registerBlockAddedRequest(graft::Router &router)
{
    Router::Handler3 h3(nullptr, blockAddedHandler, nullptr);

    router.addRoute(PATH, METHOD_POST, h3);

    LOG_PRINT_L0("route " << PATH << " registered");
}

}
