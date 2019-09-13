#pragma once

#include "lib/graft/router.h"
#include "lib/graft/inout.h"
#include "lib/graft/jsonrpc.h"

namespace graft::supernode::request {

GRAFT_DEFINE_IO_STRUCT_INITED(BlockchainBasedListTierEntry,
                              (std::string, supernode_public_id, std::string()),
                              (std::string, supernode_public_address, std::string()),
                              (uint64_t,    amount, 0)
                       );

GRAFT_DEFINE_IO_STRUCT_INITED(BlockchainBasedListTier,
                              (std::vector<BlockchainBasedListTierEntry>, supernodes, std::vector<BlockchainBasedListTierEntry>())
                       );

GRAFT_DEFINE_IO_STRUCT_INITED(BlockchainBasedList,
                              (uint64_t, current_height, uint64_t()),
                              (uint64_t, start_height, uint64_t()),
                              (uint64_t, end_height, uint64_t()),
                              (std::string, block_hash, std::string()),
                              (std::vector<BlockchainBasedListTier>, tiers, std::vector<BlockchainBasedListTier>())
                       );

GRAFT_DEFINE_IO_STRUCT_INITED(BlockchainBasedListResponse,
                              (int, Status, 0)
                              );

GRAFT_DEFINE_JSON_RPC_REQUEST(BlockchainBasedListJsonRpcRequest, BlockchainBasedList);
GRAFT_DEFINE_JSON_RPC_RESPONSE_RESULT(BlockchainBasedListJsonRpcResponse, BlockchainBasedListResponse);


Status blockchainBasedListHandler(const Router::vars_t& vars, const graft::Input& input, graft::Context& ctx, graft::Output& output) noexcept;
Status pingResultHandler (const Router::vars_t& vars, const graft::Input& input, graft::Context& ctx, graft::Output& output);
Status votesHandlerV1 (const Router::vars_t& vars, const graft::Input& input, graft::Context& ctx, graft::Output& output);

} // namespace


