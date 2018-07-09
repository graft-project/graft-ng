#ifndef SALEREQUEST_H
#define SALEREQUEST_H

#include "router.h"
#include "jsonrpc.h"

namespace graft {

// Sale request payload
GRAFT_DEFINE_IO_STRUCT(SaleRequest,
    (std::string, Address),
    (std::string, SaleDetails),
    (std::string, PaymentID),
    (std::string, Amount)
);


// Sale request in wrapped as json-rpc
GRAFT_DEFINE_JSON_RPC_REQUEST(SaleRequestJsonRpc, SaleRequest)

// outer structure, to be inserted into json-rpc request as "params" member
GRAFT_DEFINE_IO_STRUCT(SaleRequestP2P,
                       (std::vector<std::string>, addresses),
                       (std::string, callback_uri),
                       (SaleRequest, data) // can we have some magic that automatically serialize data to "json"->"b64" and deserialize same way
                       );


// JSON-RPC request
GRAFT_DEFINE_JSON_RPC_REQUEST(SaleRequestP2PJsonRpc, SaleRequestP2P);

// Plan "B" in case we can't do that magic, we just define:
GRAFT_DEFINE_IO_STRUCT(MulticastRequest,
                       (std::vector<std::string>, addresses),
                       (std::string, callback_uri),
                       (std::string, data)
                       );
// and
GRAFT_DEFINE_JSON_RPC_REQUEST(MulticastRequestJsonRpc, MulticastRequest);

// and manually parse "data"





GRAFT_DEFINE_IO_STRUCT(SaleResponse,
    (uint64, BlockNumber), //TODO: Need to check if we really need it.
    (std::string, PaymentID)
);

GRAFT_DEFINE_JSON_RPC_RESPONSE_RESULT(SaleResponseJsonRpc, SaleResponse);

void registerSaleRequest(graft::Router &router);

}

#endif // SALEREQUEST_H
