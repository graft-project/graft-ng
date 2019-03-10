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

#include "supernode/requests/blockchain_based_list.h"
#include "supernode/requestdefines.h"
#include "rta/fullsupernodelist.h"
#include "rta/supernode.h"

#include <misc_log_ex.h>
#include <boost/shared_ptr.hpp>

#undef MONERO_DEFAULT_LOG_CATEGORY
#define MONERO_DEFAULT_LOG_CATEGORY "supernode.blockchainbasedlistrequest"

namespace {
    static const char* PATH = "/blockchain_based_list";
}

namespace graft::supernode::request {

namespace
{

Status blockchainBasedListHandler
 (const Router::vars_t& vars,
  const graft::Input& input,
  graft::Context& ctx,
  graft::Output& output)
{
    LOG_PRINT_L1(PATH << " called with payload: " << input.data());

    boost::shared_ptr<FullSupernodeList> fsl = ctx.global.get("fsl", boost::shared_ptr<FullSupernodeList>());
    SupernodePtr supernode = ctx.global.get("supernode", SupernodePtr());

    if (!fsl.get()) {
        LOG_ERROR("Internal error. Supernode list object missing");
        return Status::Error;
    }

    if (!supernode.get()) {
       LOG_ERROR("Internal error. Supernode object missing");
       return Status::Error;
    }

    BlockchainBasedListJsonRpcRequest req;

    if (!input.get(req))
    { 
        // can't parse request
        LOG_ERROR("Failed to parse request");
        return Status::Error;
    }

      //handle tiers

    FullSupernodeList::blockchain_based_list tiers;

    for (const BlockchainBasedListTier& tier : req.params.tiers)
    {
        const std::vector<BlockchainBasedListTierEntry>& supernode_descs = tier.supernodes;

        FullSupernodeList::blockchain_based_list_tier supernodes;

        supernodes.reserve(supernode_descs.size());

        for (const BlockchainBasedListTierEntry& supernode_desc : supernode_descs)
        {
            FullSupernodeList::blockchain_based_list_entry entry;

            entry.supernode_public_id      = supernode_desc.supernode_public_id;
            entry.supernode_public_address = supernode_desc.supernode_public_address;
            entry.amount                   = supernode_desc.amount;

            supernodes.emplace_back(std::move(entry));
        }

        tiers.emplace_back(std::move(supernodes));
    }

    fsl->setBlockchainBasedList(req.params.block_number, FullSupernodeList::blockchain_based_list_ptr(
      new FullSupernodeList::blockchain_based_list(std::move(tiers))));

    return Status::Ok;
}

}

void registerBlockchainBasedListRequest(graft::Router &router)
{
    Router::Handler3 h3(nullptr, blockchainBasedListHandler, nullptr);

    router.addRoute(PATH, METHOD_POST, h3);

    LOG_PRINT_L0("route " << PATH << " registered");
}

}
