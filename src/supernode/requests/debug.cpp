
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
#define MONERO_DEFAULT_LOG_CATEGORY "supernode.payrequest"

namespace graft::supernode::request::debug {

GRAFT_DEFINE_IO_STRUCT(DbSupernode,
    (std::string, Address),
    (uint64, StakeAmount),
    (uint64, LastUpdateAge)
);

GRAFT_DEFINE_IO_STRUCT(SupernodeListResponse,
    (std::vector<DbSupernode>, items)
);

GRAFT_DEFINE_JSON_RPC_RESPONSE_RESULT(SupernodeListJsonRpcResult, SupernodeListResponse);

Status getSupernodeList(const Router::vars_t& vars, const graft::Input& input,
                        graft::Context& ctx, graft::Output& output)
{



    FullSupernodeListPtr fsl = ctx.global.get(CONTEXT_KEY_FULLSUPERNODELIST, FullSupernodeListPtr());
    auto supernodes = fsl->items();

    SupernodeListJsonRpcResult resp;
    for (auto& sa : supernodes)
    {
        auto sPtr = fsl->get(sa);
        DbSupernode dbSupernode;
        dbSupernode.Address = sPtr->walletAddress();
        dbSupernode.StakeAmount = sPtr->stakeAmount();
        dbSupernode.LastUpdateAge = static_cast<unsigned>(std::time(nullptr)) - sPtr->lastUpdateTime();
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

    uint64_t height;
    try
    {
        height = stoull(vars.find("height")->second);
    }
    catch(...)
    {
        return errorInternalError("invalid input", output);
    }

    const bool ok = fsl->buildAuthSample(height, sample);
    if(!ok)
    {
        return errorInternalError("failed to build auth sample", output);
    }

    SupernodeListJsonRpcResult resp;
    for(auto& sPtr : sample)
    {
        DbSupernode sn;
        sn.Address = sPtr->walletAddress();
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

void __registerDebugRequests(Router &router)
{
#define _HANDLER(h) {nullptr, graft::supernode::request::debug::h, nullptr, "", 0}

    router.addRoute("/debug/supernode_list", METHOD_GET, _HANDLER(getSupernodeList));
    router.addRoute("/debug/auth_sample/{height:[0-9]+}", METHOD_GET, _HANDLER(getAuthSample));
    router.addRoute("/debug/announce", METHOD_POST, _HANDLER(doAnnounce));
}

}

