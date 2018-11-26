
#pragma once

#include "lib/graft/router.h"
#include "lib/graft/jsonrpc.h"

namespace graft::supernode::request {

//GRAFT_DEFINE_IO_STRUCT_INITED(GetInfoResult,
//                              (uint64_t, alt_blocks_count, 0),
//                              (uint64_t, difficulty, 0),
//                              (uint64_t, grey_peerlist_size,  0),
//                              (uint64_t, height, 0),
//                              (uint64_t, incoming_connections_count, 0),
//                              (uint64_t, outgoing_connections_count, 0),
//                              (std::string, status, "OK"),
//                              (uint64_t, target, 0),
//                              (uint64_t, target_height, 0),
//                              (bool, testnet, false),
//                              (std::string, top_block_hash, ""),
//                              (uint64_t, tx_count, 0),
//                              (uint64_t, tx_pool_size, 0),
//                              (uint64_t, white_peerlist_size, 0)
//                              );

// No structure for request

GRAFT_DEFINE_IO_STRUCT_INITED(GetInfoResponse,
                              (uint64_t, alt_blocks_count, 0),
                              (uint64_t, difficulty, 0),
                              (uint64_t, grey_peerlist_size,  0),
                              (uint64_t, height, 0),
                              (uint64_t, incoming_connections_count, 0),
                              (uint64_t, outgoing_connections_count, 0),
                              (std::string, status, "OK"),
                              (uint64_t, target, 0),
                              (uint64_t, target_height, 0),
                              (bool, testnet, false),
                              (std::string, top_block_hash, ""),
                              (uint64_t, tx_count, 0),
                              (uint64_t, tx_pool_size, 0),
                              (uint64_t, white_peerlist_size, 0)
                              );

void registerGetInfoRequest(graft::Router& router);

}

