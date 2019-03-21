
#include "lib/graft/inout.h"
#include "lib/graft/jsonrpc.h"
#include "supernode/requests/broadcast.h"
#include "supernode/requests/multicast.h"
#include "supernode/requests/send_supernode_announce.h"
#include "supernode/requests/pay.h"
#include "supernode/requestdefines.h"
#include "lib/graft/requesttools.h"
#include "rta/supernode.h"
#include "rta/fullsupernodelist.h"

#include <misc_log_ex.h>
#include <cryptonote_protocol/blobdatatype.h>
#include <cryptonote_basic/cryptonote_format_utils.h>

#include <string>

#undef MONERO_DEFAULT_LOG_CATEGORY
#define MONERO_DEFAULT_LOG_CATEGORY "supernode.debugrequest"

namespace graft::supernode::request::debug {

GRAFT_DEFINE_IO_STRUCT(DbSupernode,
    (std::string, Address),
    (std::string, PublicId),
    (uint64, StakeAmount),
    (uint64, StakeFirstValidBlock),
    (uint64, StakeExpiringBlock),
    (bool, IsStakeValid),
    (unsigned int, BlockchainBasedListTier),
    (unsigned int, AuthSampleBlockchainBasedListTier),
    (bool, IsAvailableForAuthSample),
    (uint64, LastUpdateAge)
);

GRAFT_DEFINE_IO_STRUCT(SupernodeListResponse,
    (std::vector<DbSupernode>, items),
    (bool, has_blockchain_based_list),
    (uint64_t, height)
);

GRAFT_DEFINE_JSON_RPC_RESPONSE_RESULT(SupernodeListJsonRpcResult, SupernodeListResponse);

Status getSupernodeList(const Router::vars_t& vars, const graft::Input& input,
                        graft::Context& ctx, graft::Output& output)
{

    bool validOnly = true;

    try {
        validOnly = stoul(vars.find("all")->second) == 0;
    } catch (...) {
        return errorInternalError("invalid input", output);
    }

    FullSupernodeListPtr fsl = ctx.global.get(CONTEXT_KEY_FULLSUPERNODELIST, FullSupernodeListPtr());
    auto supernodes = fsl->items();

    SupernodeListJsonRpcResult resp;

    resp.result.height = fsl->getBlockchainBasedListMaxBlockNumber();
    resp.result.has_blockchain_based_list = fsl->hasBlockchainBasedList(resp.result.height);

    FullSupernodeList::blockchain_based_list auth_sample_base_list;

    uint64_t auth_sample_base_block_number = fsl->getBlockchainBasedListForAuthSample(resp.result.height, auth_sample_base_list);

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

    for (auto& sa : supernodes)
    {
        auto sPtr = fsl->get(sa);
        MDEBUG("checking supernode: " << sPtr->walletAddress());
        if (sPtr->busy()) {
            MDEBUG("supernode: " << sPtr->walletAddress() << " is currently busy");
            continue;
        }
        uint64_t lastUpdateAge = static_cast<unsigned>(std::time(nullptr)) - sPtr->lastUpdateTime();
        if (validOnly && !(lastUpdateAge <= FullSupernodeList::ANNOUNCE_TTL_SECONDS
                           && sPtr->stakeAmount())) {
            MDEBUG("supernode " << sPtr->walletAddress() << " can't be considered as valid one");
            continue;
        }

        DbSupernode dbSupernode;
        dbSupernode.LastUpdateAge = lastUpdateAge;
        dbSupernode.Address = sPtr->walletAddress();
        dbSupernode.PublicId = sPtr->idKeyAsString();
        dbSupernode.StakeAmount = sPtr->stakeAmount();
        dbSupernode.StakeFirstValidBlock = sPtr->stakeBlockHeight();
        dbSupernode.StakeExpiringBlock = sPtr->stakeBlockHeight() + sPtr->stakeUnlockTime();
        dbSupernode.IsStakeValid = resp.result.height >= dbSupernode.StakeFirstValidBlock && resp.result.height < dbSupernode.StakeExpiringBlock;
        dbSupernode.BlockchainBasedListTier = fsl->getSupernodeBlockchainBasedListTier(dbSupernode.PublicId, resp.result.height);
        dbSupernode.AuthSampleBlockchainBasedListTier = fsl->getSupernodeBlockchainBasedListTier(dbSupernode.PublicId, auth_sample_base_block_number);
        dbSupernode.IsAvailableForAuthSample = is_supernode_available(dbSupernode.PublicId);

        resp.result.items.push_back(dbSupernode);
    }
    output.load(resp);
    return Status::Ok;
}

Status getAuthSample(const Router::vars_t& vars, const graft::Input& input,
                        graft::Context& ctx, graft::Output& output)
{


    FullSupernodeListPtr fsl = ctx.global.get(CONTEXT_KEY_FULLSUPERNODELIST, FullSupernodeListPtr());
    std::vector<SupernodePtr> sample;
    uint64_t sample_block_number = 0;

    std::string payment_id;
    try
    {
        payment_id = vars.find("payment_id")->second;
    }
    catch(...)
    {
        return errorInternalError("invalid input", output);
    }

    SupernodeListJsonRpcResult resp;

    const bool ok = fsl->buildAuthSample(fsl->getBlockchainBasedListMaxBlockNumber(), payment_id, sample, sample_block_number);
    if(!ok)
    {
        return errorInternalError("failed to build auth sample", output);
    }

    resp.result.height = sample_block_number;

    for(auto& sPtr : sample)
    {
        DbSupernode sn;
        sn.Address = sPtr->walletAddress();
        sn.PublicId = sPtr->idKeyAsString();
        sn.StakeAmount = sPtr->stakeAmount();
        sn.LastUpdateAge = static_cast<unsigned>(std::time(nullptr)) - sPtr->lastUpdateTime();
        resp.result.items.push_back(sn);
    }

    output.load(resp);

    return Status::Ok;
}

Status doAnnounce(const Router::vars_t& vars, const graft::Input& input,
                        graft::Context& ctx, graft::Output& output)
{
    return sendAnnounce(vars, input, ctx, output);
}


Status closeStakeWallets(const Router::vars_t& vars, const graft::Input& input,
                        graft::Context& ctx, graft::Output& output)
{
    FullSupernodeListPtr fsl = ctx.global.get(CONTEXT_KEY_FULLSUPERNODELIST, FullSupernodeListPtr());
    auto items = fsl->items();
    for (const auto &sn : items) {
        fsl->remove(sn);
    }
    return Status::Ok;
}

enum BlockchainBasedListMode
{
  BlockchainBasedListMode_Source,
  BlockchainBasedListMode_ForAuthSample,
};

GRAFT_DEFINE_IO_STRUCT(DbgBlockchainBasedListEntry,
    (std::string, Address),
    (std::string, PublicId),
    (uint64, StakeAmount)
);

GRAFT_DEFINE_IO_STRUCT(DbgBlockchainBasedListResponse,
    (std::vector<std::vector<DbgBlockchainBasedListEntry> >, Items),
    (uint64_t, Height)
);

GRAFT_DEFINE_JSON_RPC_RESPONSE_RESULT(DbgBlockchainBasedListResponseJsonRpcResult, DbgBlockchainBasedListResponse);

Status getBlockchainBasedListImpl(const Router::vars_t& vars, const graft::Input& input, graft::Context& ctx, graft::Output& output, BlockchainBasedListMode mode)
{
    uint64_t block_height = 0;

    try {
        block_height = stoull(vars.find("block_height")->second);
    } catch (...) {
        return errorInternalError("invalid input", output);
    }

    FullSupernodeListPtr fsl = ctx.global.get(CONTEXT_KEY_FULLSUPERNODELIST, FullSupernodeListPtr());

    DbgBlockchainBasedListResponseJsonRpcResult resp;

    FullSupernodeList::blockchain_based_list bbl;

    switch (mode)
    {
        case BlockchainBasedListMode_ForAuthSample:
            resp.result.Height = fsl->getBlockchainBasedListForAuthSample(block_height, bbl);

            if (!resp.result.Height)
            {
                return errorInternalError("blockchain based list has not been found", output);
            }

            break;
        case BlockchainBasedListMode_Source:
        {
            FullSupernodeList::blockchain_based_list_ptr bbl_ptr = fsl->findBlockchainBasedList(block_height);

            if (!bbl_ptr)
            {
                return errorInternalError("blockchain based list has not been found", output);
            }

            resp.result.Height = block_height;
            bbl                = *bbl_ptr;

            break;
        }
    }

    for (const FullSupernodeList::blockchain_based_list_tier& src_tier : bbl)
    {
        std::vector<DbgBlockchainBasedListEntry> dst_tier;

        for (const FullSupernodeList::blockchain_based_list_entry& src_sn : src_tier)
        {
            DbgBlockchainBasedListEntry dst_sn;

            dst_sn.Address     = src_sn.supernode_public_address;
            dst_sn.PublicId    = src_sn.supernode_public_id;
            dst_sn.StakeAmount = src_sn.amount;

            dst_tier.emplace_back(std::move(dst_sn));
        }

        resp.result.Items.emplace_back(std::move(dst_tier));        
    }

    output.load(resp);

    return Status::Ok;
}

template <BlockchainBasedListMode mode>
Status getBlockchainBasedList(const Router::vars_t& vars, const graft::Input& input, graft::Context& ctx, graft::Output& output)
{
  return getBlockchainBasedListImpl(vars, input, ctx, output, mode);
}

void __registerDebugRequests(Router &router)
{
#define _HANDLER(h) {nullptr, graft::supernode::request::debug::h, nullptr}
    // /debug/supernode_list/0 -> do not include inactive items
    // /debug/supernode_list/1 -> include inactive items
    router.addRoute("/debug/supernode_list/{all:[0-1]}", METHOD_GET, _HANDLER(getSupernodeList));
    router.addRoute("/debug/blockchain_based_list/{block_height:[0-9]+}", METHOD_GET, _HANDLER(getBlockchainBasedList<BlockchainBasedListMode_Source>));
    router.addRoute("/debug/auth_sample_blockchain_based_list/{block_height:[0-9]+}", METHOD_GET, _HANDLER(getBlockchainBasedList<BlockchainBasedListMode_ForAuthSample>));
    router.addRoute("/debug/announce", METHOD_POST, _HANDLER(doAnnounce));
    router.addRoute("/debug/close_wallets/", METHOD_POST, _HANDLER(closeStakeWallets));
    router.addRoute("/debug/auth_sample/{payment_id:[0-9a-zA-Z]+}", METHOD_GET, _HANDLER(getAuthSample));
}

}
