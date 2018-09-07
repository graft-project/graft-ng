#include "payrequest.h"
#include "requestdefines.h"
#include "requesttools.h"
#include "requests/broadcast.h"
#include "requests/multicast.h"
#include "rta/supernode.h"
#include "rta/fullsupernodelist.h"
#include "inout.h"
#include "jsonrpc.h"

#include <misc_log_ex.h>
#include <cryptonote_protocol/blobdatatype.h>
#include <cryptonote_basic/cryptonote_format_utils.h>

#include <string>

#undef MONERO_DEFAULT_LOG_CATEGORY
#define MONERO_DEFAULT_LOG_CATEGORY "supernode.debug"

namespace graft { namespace requests { namespace debug {

Status getSupernodeList(const Router::vars_t& vars, const graft::Input& input,
                        graft::Context& ctx, graft::Output& output)
{
    GRAFT_DEFINE_IO_STRUCT(DbSupernode,
        (std::string, Address),
        (uint64, Fee),
        (uint64, LastUpdateAge)
    );

    GRAFT_DEFINE_IO_STRUCT(DbSupernodeList,
        (std::vector<DbSupernode>, Supernodes)
    );

    FullSupernodeListPtr fsl = ctx.global.get(CONTEXT_KEY_FULLSUPERNODELIST, FullSupernodeListPtr());
    auto supernodes = fsl->items();

    DbSupernodeList dbSupernodeList;
    for (auto& sa : supernodes)
    {
        auto sPtr = fsl->get(sa);

        DbSupernode dbSupernode;
        dbSupernode.Address = sPtr->walletAddress();
        dbSupernode.Fee = 0;
        dbSupernode.LastUpdateAge = sPtr->lastUpdateTime();

        dbSupernodeList.Supernodes.push_back(dbSupernode);
    }

    output.load(dbSupernodeList);

    return Status::Ok;
}

Status getAuthSample(const Router::vars_t& vars, const graft::Input& input,
                        graft::Context& ctx, graft::Output& output)
{
    GRAFT_DEFINE_IO_STRUCT(AsSupernode,
        (std::string, Address),
        (uint64, Fee)
    );

    GRAFT_DEFINE_IO_STRUCT(AuthSample,
        (std::vector<AsSupernode>, AuthSample)
    );

    FullSupernodeListPtr fsl = ctx.global.get(CONTEXT_KEY_FULLSUPERNODELIST, FullSupernodeListPtr());
    std::vector<SupernodePtr> sample;

    uint64_t height;
    try {
        height = stoull(vars.find("height")->second);
    } catch (...) {
        return Status::Error;
    }

    bool ok = fsl->buildAuthSample(height, sample);
    if (!ok)
        return Status::Error;

    AuthSample authSample;
    for (auto& sPtr : sample)
    {
        AsSupernode asSupernode;
        asSupernode.Address = sPtr->walletAddress();
        asSupernode.Fee = 0;

        authSample.AuthSample.push_back(asSupernode);
    }

    output.load(authSample);

    return Status::Ok;
}
}}

void __registerDebugRequests(Router &router)
{
#define _HANDLER(h) {nullptr, requests::debug::h, nullptr}

    router.addRoute("/debug/supernode_list", METHOD_GET, _HANDLER(getSupernodeList));
    router.addRoute("/debug/auth_sample/{height:[0-9]+}", METHOD_GET, _HANDLER(getAuthSample));
}

}
