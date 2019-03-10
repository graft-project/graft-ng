#pragma once

#include "lib/graft/router.h"
#include "lib/graft/inout.h"
#include "lib/graft/jsonrpc.h"

namespace graft::supernode::request {

GRAFT_DEFINE_IO_STRUCT_INITED(SupernodeStake,
                              (uint64_t, amount, 0),
                              (uint32_t, tier, 0),
                              (uint64_t, block_height, 0),
                              (uint64_t, unlock_time, 0),
                              (std::string, supernode_public_id, std::string()),
                              (std::string, supernode_public_address, std::string())
                       );

GRAFT_DEFINE_IO_STRUCT_INITED(SupernodeStakes,
                              (std::vector<SupernodeStake>, stakes, std::vector<SupernodeStake>())
                       );

GRAFT_DEFINE_IO_STRUCT_INITED(SendSupernodeStakesResponse,
                              (int, status, 0)
                              );

GRAFT_DEFINE_JSON_RPC_REQUEST(SendSupernodeStakesJsonRpcRequest, SupernodeStakes);
GRAFT_DEFINE_JSON_RPC_RESPONSE_RESULT(SendSupernodeStakesJsonRpcResponse, SendSupernodeStakesResponse);

void registerSendSupernodeStakesRequest(graft::Router &router);

}
