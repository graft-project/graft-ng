#pragma once

#include "router.h"
#include "jsonrpc.h"

namespace graft {

class Context;

GRAFT_DEFINE_IO_STRUCT_INITED(WalletBalanceRequest,
    (std::string, WalletId, std::string()),
    (std::string, Account,  std::string()),
    (std::string, Password, std::string())
);

GRAFT_DEFINE_JSON_RPC_REQUEST(WalletBalanceRequestJsonRpc, WalletBalanceRequest)

GRAFT_DEFINE_IO_STRUCT_INITED(WalletBalanceResponse,
    (int, Result, 0)
);

GRAFT_DEFINE_JSON_RPC_REQUEST(WalletBalanceResponseJsonRpc, WalletBalanceResponse)

}
