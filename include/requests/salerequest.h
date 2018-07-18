#ifndef SALEREQUEST_H
#define SALEREQUEST_H

#include "router.h"
#include "jsonrpc.h"
#include "requestdefines.h"


namespace graft {

// Sale request payload
GRAFT_DEFINE_IO_STRUCT(SaleRequest,
    (std::string, Address),
    (std::string, SaleDetails),
    (std::string, PaymentID),
    (uint64_t, Amount)
);


// Sale request in wrapped as json-rpc
GRAFT_DEFINE_JSON_RPC_REQUEST(SaleRequestJsonRpc, SaleRequest)

// JSON-RPC request
// GRAFT_DEFINE_JSON_RPC_REQUEST(SaleRequestP2PJsonRpc, SaleRequestP2P);

GRAFT_DEFINE_IO_STRUCT(SaleResponse,
    (uint64, BlockNumber), //TODO: Need to check if we really need it.
    (std::string, PaymentID)
);

GRAFT_DEFINE_JSON_RPC_RESPONSE_RESULT(SaleResponseJsonRpc, SaleResponse);

void registerSaleRequest(graft::Router &router);

}

#endif // SALEREQUEST_H
