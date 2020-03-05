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

#include "getsupernodeinfo.h"

#include "supernode/requests/common.h"
#include "supernode/requests/broadcast.h"

#include "supernode/requestdefines.h"
#include "lib/graft/requesttools.h"
#include "rta/supernode.h"
#include "rta/fullsupernodelist.h"


#include <cryptonote_basic/cryptonote_basic.h>
#include <cryptonote_basic/cryptonote_format_utils.h>
#include <utils/cryptmsg.h> // one-to-many message cryptography

#include <string>

#undef MONERO_DEFAULT_LOG_CATEGORY
#define MONERO_DEFAULT_LOG_CATEGORY "supernode.payrequest"

namespace graft::supernode::request {





// TODO: merge with /debug handler, now it copy-pasted from there

Status handleSupernodeInfoRequest(const Router::vars_t &vars, const Input &input, Context &ctx, Output &output)
{

    SupernodeInfoRequest req;
    if (!input.get(req)) {
        return errorInvalidParams(output);
    }



    FullSupernodeListPtr fsl = ctx.global.get(CONTEXT_KEY_FULLSUPERNODELIST, FullSupernodeListPtr());

    FullSupernodeList::blockchain_based_list auth_sample_base_list;

    uint64_t bbl_block_height = fsl->getBlockchainBasedListMaxBlockNumber();
    uint64_t auth_sample_base_block_number = fsl->getBlockchainBasedListForAuthSample(bbl_block_height, auth_sample_base_list);

    auto is_supernode_available = [&](const std::string& supernode_public_id)
    {
        for (const FullSupernodeList::blockchain_based_list_tier& tier : auth_sample_base_list)
        {
            for (const FullSupernodeList::blockchain_based_list_entry& entry : tier)
                if (supernode_public_id == entry.supernode_public_id)
                    return true;
        }

        return false;
    };


    SupernodeInfoResponse resp;
    for (const std::string &id : req.input) {
        SupernodePtr sn = fsl->get(id);
        if (sn) {
            SupernodeInfo  sni;
            sni.Address = sn->walletAddress();
            sni.PublicId = sn->idKeyAsString();
            sni.StakeAmount = sn->stakeAmount();
            sni.StakeFirstValidBlock = sn->stakeBlockHeight();
            sni.StakeExpiringBlock = sn->stakeBlockHeight() + sn->stakeUnlockTime();
            sni.IsStakeValid = bbl_block_height >= sni.StakeFirstValidBlock && bbl_block_height < sni.StakeExpiringBlock;
            sni.LastUpdateAge = static_cast<unsigned>(std::time(nullptr)) - sn->lastUpdateTime();
            sni.BlockchainBasedListTier = fsl->getSupernodeBlockchainBasedListTier(id, bbl_block_height);
            sni.IsAvailableForAuthSample = is_supernode_available(id);
            resp.output.emplace_back(sni);
        }
    }

    output.load(resp);
    return Status::Ok;

}

} // namespace graft::supernode::request

