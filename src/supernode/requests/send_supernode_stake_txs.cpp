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

bool verifySignature(const std::string& id_key, const std::string& wallet_public_address, const std::string& signature)
{
    crypto::public_key W;
    if (!epee::string_tools::hex_to_pod(id_key, W))
    {
        LOG_ERROR("Invalid supernode public identifier '" << id_key << "'");
        return false;
    }

    crypto::signature sign;
    if (!epee::string_tools::hex_to_pod(signature, sign))
    {
        LOG_ERROR("Invalid supernode signature '" << signature << "'");
        return false;
    }

    std::string data = wallet_public_address + ":" + id_key;
    crypto::hash hash;
    crypto::cn_fast_hash(data.data(), data.size(), hash);
    return crypto::check_signature(hash, W, sign);
}

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
    const std::vector<SupernodeStakeTransaction>& stake_txs = req.params.stake_txs;

    for (const SupernodeStakeTransaction& tx : stake_txs)
    {
        if (!fsl->exists(tx.supernode_public_address))
            continue;

        // check if supernode currently busy
        SupernodePtr sn = fsl->get(tx.supernode_public_address);

        if (sn->busy()) {
            MWARNING("Unable to update supernode with new stake transactions: " << tx.supernode_public_address << ", BUSY");
            return Status::Error;
        }

        if (!verifySignature(tx.supernode_public_id, tx.supernode_public_address, tx.supernode_signature))
        {
           LOG_ERROR("Supernode signature failed for supernode_public_id='" << tx.supernode_public_id <<"', supernode_public_address='" <<
               tx.supernode_public_address << "', signature='" << tx.supernode_signature << "'");
           return Status::Error;
        }

        sn->setStakeTransactionBlockHeight(tx.block_height);
        sn->setStakeTransactionUnlockTime(tx.unlock_time);
    }

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
