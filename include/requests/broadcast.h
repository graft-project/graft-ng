#ifndef BROADCAST_H
#define BROADCAST_H

#include "inout.h"
#include "jsonrpc.h"
#include "requestdefines.h"

#include <vector>
#include <string>


namespace graft {

// Plan "B" in case we can't do that magic, we just define:
GRAFT_DEFINE_IO_STRUCT(BroadcastRequest,
                       (std::strint, sender_address),
                       (std::string, callback_uri),
                       (std::string, data)
                       );
// and
GRAFT_DEFINE_JSON_RPC_REQUEST(BroadcastRequestJsonRpc, BroadcastRequest);

// and manually parse "data"

GRAFT_DEFINE_IO_STRUCT_INITED(BroadcastResponseToCryptonode,
                       (std::string, status, "OK"));

GRAFT_DEFINE_IO_STRUCT_INITED(BroadcastResponseFromCryptonode,
                       (int, status, STATUS_OK));

GRAFT_DEFINE_JSON_RPC_RESPONSE_RESULT(BroadcastResponseToCryptonodeJsonRpc, BroadcastResponseToCryptonode);

GRAFT_DEFINE_JSON_RPC_RESPONSE(BroadcastResponseFromCryptonodeJsonRpc, BroadcastResponseFromCryptonode);


}


#endif // BROADCAST_H
