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

#include "supernode/requests/send_supernode_stake_txs.h"
#include "supernode/requestdefines.h"
#include "rta/fullsupernodelist.h"
#include "rta/supernode.h"

#include <misc_log_ex.h>
#include <boost/shared_ptr.hpp>

#undef MONERO_DEFAULT_LOG_CATEGORY
#define MONERO_DEFAULT_LOG_CATEGORY "supernode.sendsupernodestaketxsrequest"

namespace {
    static const char* PATH = "/send_supernode_stake_txs";
}

namespace graft::supernode::request {

namespace
{

Status supernodeStakeTransactionsHandler
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

    SendSupernodeStakeTransactionsJsonRpcRequest req;

    if (!input.get(req))
    { 
        // can't parse request
        LOG_ERROR("Failed to parse request");
        return Status::Error;
    }

    //  handle stake Transactions
    const std::vector<SupernodeStakeTransaction>& src_stake_txs = req.params.stake_txs;
    FullSupernodeList::stake_transaction_array dst_stake_txs;

    dst_stake_txs.reserve(src_stake_txs.size());

    for (const SupernodeStakeTransaction& src_tx : src_stake_txs)
    {
        stake_transaction dst_tx;

        dst_tx.amount                   = src_tx.amount;
        dst_tx.block_height             = src_tx.block_height;
        dst_tx.unlock_time              = src_tx.unlock_time;
        dst_tx.supernode_public_id      = src_tx.supernode_public_id;
        dst_tx.supernode_public_address = src_tx.supernode_public_address;

        dst_stake_txs.emplace_back(std::move(dst_tx));
    }

    std::string cryptonode_rpc_address = ctx.global["cryptonode_rpc_address"];
    bool testnet = ctx.global["testnet"];

    fsl->updateStakeTransactions(dst_stake_txs, cryptonode_rpc_address, testnet);

    return Status::Ok;
}

}

void registerSendSupernodeStakeTransactionsRequest(graft::Router &router)
{
    Router::Handler3 h3(nullptr, supernodeStakeTransactionsHandler, nullptr);

    router.addRoute(PATH, METHOD_POST, h3);

    LOG_PRINT_L0("route " << PATH << " registered");
}

}
