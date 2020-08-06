#pragma once

#include "lib/graft/router.h"
#include "lib/graft/inout.h"
#include "lib/graft/jsonrpc.h"

namespace graft::supernode::request {


GRAFT_DEFINE_IO_STRUCT_INITED(BlockAddedRequest,
                              (uint64_t, height, uint64_t()),
                              (std::string, block_hash, std::string()),
                              (std::string, seed_hash, std::string())
                       );

GRAFT_DEFINE_IO_STRUCT_INITED(BlockAddedResponse,
                              (int, status, 0)
                              );

GRAFT_DEFINE_JSON_RPC_REQUEST(BlockAddedJsonRpcRequest, BlockAddedRequest);
GRAFT_DEFINE_JSON_RPC_RESPONSE_RESULT(BlockAddedJsonRpcResponse, BlockAddedResponse);

void registerBlockAddedRequest(graft::Router &router);

}
