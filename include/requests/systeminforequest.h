#ifndef SYSTEMINFOREQUEST_H
#define SYSTEMINFOREQUEST_H

//#include <vector>
#include <string>

#include "jsonrpc.h"
#include "router.h"

namespace graft {

GRAFT_DEFINE_IO_STRUCT_INITED(SystemInfoRequest,
    (int, Request, 0)
);

// SystemInfo request in wrapped as json-rpc
GRAFT_DEFINE_JSON_RPC_REQUEST(SystemInfoJsonRpc, SystemInfoRequest)

GRAFT_DEFINE_IO_STRUCT_INITED(SystemInfoResponse,
    (std::string, PaymentID, std::string()),
    (std::string, Address, std::string()),
    (uint64, BlockNumber, 0), //TODO: Need to check if we really need it.
    (uint64, Amount, 0),
    (std::vector<std::string>, Transactions, std::vector<std::string>())
);

GRAFT_DEFINE_JSON_RPC_RESPONSE_RESULT(SystemInfoJsonRpc, SystemInfoResponse);


void register_system_info_request(Router& router);

}

#endif // SYSTEMINFOREQUEST_H
