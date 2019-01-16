
#pragma once

#include "lib/graft/jsonrpc.h"
#include "supernode/requestdefines.h"

#include <vector>
#include <string>

namespace graft::supernode::request {

// Plan "B" in case we can't do that magic, we just define:
GRAFT_DEFINE_IO_STRUCT(MulticastRequest,
                       (std::string, sender_address),
                       (std::vector<std::string>, receiver_addresses),
                       (std::string, callback_uri),
                       (std::string, data)
                       );
// and
GRAFT_DEFINE_JSON_RPC_REQUEST(MulticastRequestJsonRpc, MulticastRequest);

// and manually parse "data"

GRAFT_DEFINE_IO_STRUCT_INITED(MulticastResponseToCryptonode,
                       (std::string, status, "OK"));

GRAFT_DEFINE_IO_STRUCT_INITED(MulticastResponseFromCryptonode,
                       (int, status, STATUS_OK));

GRAFT_DEFINE_JSON_RPC_RESPONSE_RESULT(MulticastResponseToCryptonodeJsonRpc, MulticastResponseToCryptonode);

GRAFT_DEFINE_JSON_RPC_RESPONSE(MulticastResponseFromCryptonodeJsonRpc, MulticastResponseFromCryptonode);

bool createMulticastRequest(const std::vector<std::string> &receiver_addresses,
                            const std::string &sender_address,
                            const std::string &callback_uri,
                            const std::string &data,
                            const std::string &path,
                            graft::Output& output)
{
    MulticastRequestJsonRpc req;
    req.method = "multicast";
    req.params.receiver_addresses = receiver_addresses;
    req.params.sender_address = sender_address;
    req.params.callback_uri = callback_uri;
    req.params.data = data;
    output.load(req);
    output.path = path;
    output.uri = DEFAULT_COMMUNICATION_PROTOCOL;
    return true;
}

}

