#pragma once

#include "router.h"
#include "jsonrpc.h"

namespace graft {

class Context;

GRAFT_DEFINE_IO_STRUCT_INITED(WalletRestoreAccountRequest,
    (std::string, Password, std::string()),
    (std::string, Seed,     std::string())
);

GRAFT_DEFINE_JSON_RPC_REQUEST(WalletRestoreAccountRequestJsonRpc, WalletRestoreAccountRequest)

GRAFT_DEFINE_IO_STRUCT_INITED(WalletRestoreAccountResponse,
    (int, Result, 0)
);

GRAFT_DEFINE_JSON_RPC_REQUEST(WalletRestoreAccountResponseJsonRpc, WalletRestoreAccountResponse)

}
