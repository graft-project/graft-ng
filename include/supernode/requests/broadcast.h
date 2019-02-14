
#pragma once

#include "lib/graft/jsonrpc.h"
#include "supernode/requestdefines.h"

#include <string>

namespace graft::supernode::request {

GRAFT_DEFINE_IO_STRUCT(BroadcastRequest,
                       (std::string, sender_address),
                       (std::string, callback_uri),
                       (std::string, data)
                       );

GRAFT_DEFINE_JSON_RPC_REQUEST(BroadcastRequestJsonRpc, BroadcastRequest);


GRAFT_DEFINE_IO_STRUCT_INITED(BroadcastResponseToCryptonode,
                       (std::string, status, "OK"));

GRAFT_DEFINE_IO_STRUCT_INITED(BroadcastResponseFromCryptonode,
                       (int, status, STATUS_OK));

GRAFT_DEFINE_JSON_RPC_RESPONSE_RESULT(BroadcastResponseToCryptonodeJsonRpc, BroadcastResponseToCryptonode);

GRAFT_DEFINE_JSON_RPC_RESPONSE(BroadcastResponseFromCryptonodeJsonRpc, BroadcastResponseFromCryptonode);

}

