#pragma once

#include "inout.h"
#include "jsonrpc.h"
#include "requestdefines.h"

#include <vector>
#include <string>

namespace graft {

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

