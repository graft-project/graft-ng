
#pragma once

#include "lib/graft/inout.h"
#include "lib/graft/jsonrpc.h"
#include "supernode/requestdefines.h"

#include <vector>
#include <string>

namespace graft::supernode::request {

// Plan "B" in case we can't do that magic, we just define:
GRAFT_DEFINE_IO_STRUCT_INITED(UnicastRequest,
                       (std::string, sender_address, std::string()),
                       (std::string, receiver_address, std::string()),
                       (std::string, callback_uri, std::string()),
                       (std::string, data, std::string()),
                       (bool, wait_answer, true)
                       );
// and
GRAFT_DEFINE_JSON_RPC_REQUEST(UnicastRequestJsonRpc, UnicastRequest);

// and manually parse "data"

GRAFT_DEFINE_IO_STRUCT_INITED(UnicastResponseToCryptonode,
                       (std::string, status, "OK"));

GRAFT_DEFINE_IO_STRUCT_INITED(UnicastResponseFromCryptonode,
                       (int, status, STATUS_OK));

GRAFT_DEFINE_JSON_RPC_RESPONSE_RESULT(UnicastResponseToCryptonodeJsonRpc, UnicastResponseToCryptonode);

GRAFT_DEFINE_JSON_RPC_RESPONSE(UnicastResponseFromCryptonodeJsonRpc, UnicastResponseFromCryptonode);

}

