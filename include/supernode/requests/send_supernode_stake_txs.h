#pragma once

#include "lib/graft/router.h"
#include "lib/graft/inout.h"
#include "lib/graft/jsonrpc.h"

namespace graft::supernode::request {

GRAFT_DEFINE_IO_STRUCT_INITED(SupernodeStakeTransaction,
                              (std::string, hash, std::string()),
                              (uint64_t, amount, 0),
                              (uint32_t, tier, 0),
                              (uint64_t, block_height, 0),
                              (uint64_t, unlock_time, 0),
                              (std::string, supernode_public_id, std::string()),
                              (std::string, supernode_public_address, std::string())
                       );

GRAFT_DEFINE_IO_STRUCT_INITED(SupernodeStakeTransactions,
                              (std::vector<SupernodeStakeTransaction>, stake_txs, std::vector<SupernodeStakeTransaction>())
                       );

GRAFT_DEFINE_IO_STRUCT_INITED(SendSupernodeStakeTransactionsResponse,
                              (int, status, 0)
                              );

GRAFT_DEFINE_JSON_RPC_REQUEST(SendSupernodeStakeTransactionsJsonRpcRequest, SupernodeStakeTransactions);
GRAFT_DEFINE_JSON_RPC_RESPONSE_RESULT(SendSupernodeStakeTransactionsJsonRpcResponse, SendSupernodeStakeTransactionsResponse);

void registerSendSupernodeStakeTransactionsRequest(graft::Router &router);

}
