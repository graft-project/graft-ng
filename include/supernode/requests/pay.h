
#pragma once

#include "lib/graft/router.h"
#include "lib/graft/jsonrpc.h"

namespace graft::supernode::request {

GRAFT_DEFINE_IO_STRUCT_INITED(PayRequest,
    (std::string, PaymentID, std::string()),
    (std::string, Address, std::string()),
    (uint64, BlockNumber, 0), //TODO: Need to check if we really need it.
    (uint64, Amount, 0),
    (std::vector<std::string>, Transactions, std::vector<std::string>()),
    (std::string, Account, std::string()),
    (std::string, Password, std::string())
);

// Pay request in wrapped as json-rpc
GRAFT_DEFINE_JSON_RPC_REQUEST(PayRequestJsonRpc, PayRequest)

GRAFT_DEFINE_IO_STRUCT_INITED(PayResponse,
    (int, Result, 0)
);

GRAFT_DEFINE_JSON_RPC_RESPONSE_RESULT(PayResponseJsonRpc, PayResponse);

GRAFT_DEFINE_IO_STRUCT_INITED(DestinationStruct,
                              (std::string, Address, std::string()),
                              (uint64, Amount, 0)
                              );

GRAFT_DEFINE_IO_STRUCT_INITED(PrepareTransferRequest,
    (std::string, Account, std::string()),
    (std::string, Password, std::string()),
    (std::vector<DestinationStruct>, Destinations, std::vector<DestinationStruct>())
);

GRAFT_DEFINE_IO_STRUCT_INITED(PrepareTransferResponse,
                              (int, Result, 0)
                              );

GRAFT_DEFINE_IO_STRUCT_INITED(PrepareTransferCallbackResponse,
                              (int, Result, 0),
                              (std::vector<std::string>, Transactions, std::vector<std::string>()),
                              (std::string, Fee, std::string())
                              );

void registerPayRequest(graft::Router &router);

}

