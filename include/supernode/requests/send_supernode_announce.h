
#pragma once

#include "lib/graft/router.h"
#include "lib/graft/inout.h"
#include "lib/graft/jsonrpc.h"

namespace graft::supernode::request {

GRAFT_DEFINE_IO_STRUCT_INITED(SupernodeAnnounce,
                              (std::string, supernode_public_id, std::string()),
                              (uint64_t, height, 0),
                              (std::string, signature, std::string()),
                              (std::string, network_address, std::string())
                       );


GRAFT_DEFINE_IO_STRUCT_INITED(SendSupernodeAnnounceResponse,
                              (int, Status, 0)
                              );

GRAFT_DEFINE_JSON_RPC_REQUEST(SendSupernodeAnnounceJsonRpcRequest, SupernodeAnnounce);
GRAFT_DEFINE_JSON_RPC_RESPONSE_RESULT(SendSupernodeAnnounceJsonRpcResponse, SendSupernodeAnnounceResponse);


void registerSendSupernodeAnnounceRequest(graft::Router &router);
/*!
 * \brief sendAnnounce - send supernode announce. code shared with periodic task and debug handler
 * \param vars
 * \param input
 * \param ctx
 * \param output
 * \return
 */
Status sendAnnounce(const graft::Router::vars_t& vars, const graft::Input& input, graft::Context& ctx,
        graft::Output& output);

}


