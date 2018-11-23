
#pragma once

#include "lib/graft/router.h"
#include "lib/graft/jsonrpc.h"
#include "supernode/requestdefines.h"

namespace graft::supernode::request {

// Sale request payload
GRAFT_DEFINE_IO_STRUCT_INITED(SaleRequest,
    (std::string, Address, std::string()),
    (std::string, SaleDetails, std::string()),
    (std::string, PaymentID, std::string()),
    (uint64_t, Amount, 0)
);

// Sale request in wrapped as json-rpc
GRAFT_DEFINE_JSON_RPC_REQUEST(SaleRequestJsonRpc, SaleRequest)

// JSON-RPC request
// GRAFT_DEFINE_JSON_RPC_REQUEST(SaleRequestP2PJsonRpc, SaleRequestP2P);

GRAFT_DEFINE_IO_STRUCT_INITED(SaleResponse,
    (uint64, BlockNumber, 0), //TODO: Need to check if we really need it.
    (std::string, PaymentID, std::string())
);

GRAFT_DEFINE_JSON_RPC_RESPONSE_RESULT(SaleResponseJsonRpc, SaleResponse);

void registerSaleRequest(graft::Router &router);

}

