#ifndef PAYREQUEST_H
#define PAYREQUEST_H

#include "router.h"
#include "jsonrpc.h"

namespace graft {

GRAFT_DEFINE_IO_STRUCT_INITED(PayRequest,
    (std::string, PaymentID, std::string()),
    (std::string, Address, std::string()),
    (uint64, BlockNumber, 0), //TODO: Need to check if we really need it.
    (uint64, Amount, 0),
    (std::string, tx_blob, std::string())
);

// Pay request in wrapped as json-rpc
GRAFT_DEFINE_JSON_RPC_REQUEST(PayRequestJsonRpc, PayRequest)

GRAFT_DEFINE_IO_STRUCT_INITED(PayResponse,
    (int, Result, 0)
);

GRAFT_DEFINE_JSON_RPC_RESPONSE_RESULT(PayResponseJsonRpc, PayResponse);


void registerPayRequest(graft::Router &router);

}

#endif // PAYREQUEST_H
