#pragma once

#include "lib/graft/router.h"
#include "lib/graft/inout.h"
#include "lib/graft/jsonrpc.h"

namespace graft::supernode::request {

GRAFT_DEFINE_IO_STRUCT_INITED(BlockchainBasedListTierEntry,
                              (std::string, supernode_public_id, std::string()),
                              (std::string, supernode_public_address, std::string())
                       );

GRAFT_DEFINE_IO_STRUCT_INITED(BlockchainBasedListTier,
                              (std::vector<BlockchainBasedListTierEntry>, supernodes, std::vector<BlockchainBasedListTierEntry>())
                       );

GRAFT_DEFINE_IO_STRUCT_INITED(BlockchainBasedList,
                              (uint64_t, block_number, uint64_t()),
                              (std::vector<BlockchainBasedListTier>, tiers, std::vector<BlockchainBasedListTier>())
                       );

GRAFT_DEFINE_IO_STRUCT_INITED(BlockchainBasedListResponse,
                              (int, Status, 0)
                              );

GRAFT_DEFINE_JSON_RPC_REQUEST(BlockchainBasedListJsonRpcRequest, BlockchainBasedList);
GRAFT_DEFINE_JSON_RPC_RESPONSE_RESULT(BlockchainBasedListJsonRpcResponse, BlockchainBasedListResponse);

void registerBlockchainBasedListRequest(graft::Router &router);

}
