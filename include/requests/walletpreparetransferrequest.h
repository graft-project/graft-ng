#pragma once

#include "router.h"
#include "jsonrpc.h"

namespace graft {

class Context;

GRAFT_DEFINE_IO_STRUCT_INITED(WalletTransferDestination,
                       (std::string, Address, std::string()),
                       (std::string, Amount,  std::string())
                       );

GRAFT_DEFINE_IO_STRUCT_INITED(WalletPrepareTransferRequest,
    (std::string,                            WalletId,     std::string()),
    (std::string,                            Account,      std::string()),
    (std::string,                            Password,     std::string()),
    (std::vector<WalletTransferDestination>, Destinations, std::vector<WalletTransferDestination>())
);

GRAFT_DEFINE_JSON_RPC_REQUEST(WalletPrepareTransferRequestJsonRpc, WalletPrepareTransferRequest)

GRAFT_DEFINE_IO_STRUCT_INITED(WalletPrepareTransferResponse,
    (int, Result, 0)
);

GRAFT_DEFINE_JSON_RPC_REQUEST(WalletPrepareTransferJsonRpc, WalletPrepareTransferResponse)

}
