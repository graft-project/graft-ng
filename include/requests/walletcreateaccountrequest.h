#pragma once

#include "router.h"
#include "jsonrpc.h"

namespace graft {

class Context;

GRAFT_DEFINE_IO_STRUCT_INITED(WalletCreateAccountRequest,
    (std::string, Password, std::string()),
    (std::string, Language, std::string())
);

GRAFT_DEFINE_JSON_RPC_REQUEST(WalletCreateAccountRequestJsonRpc, WalletCreateAccountRequest)

GRAFT_DEFINE_IO_STRUCT_INITED(WalletCreateAccountResponse,
    (int, Result, 0)
);

GRAFT_DEFINE_JSON_RPC_REQUEST(WalletCreateAccountResponseJsonRpc, WalletCreateAccountResponse)

}
