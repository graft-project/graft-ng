#pragma once

#include "lib/graft/router.h"
#include "lib/graft/jsonrpc.h"

namespace graft {

class Context;

namespace walletnode::request {

GRAFT_DEFINE_IO_STRUCT(WalletCreateAccountRequest,
    (std::string, Password),
    (std::string, Language)
);

GRAFT_DEFINE_JSON_RPC_REQUEST(WalletCreateAccountRequestJsonRpc, WalletCreateAccountRequest)

GRAFT_DEFINE_IO_STRUCT_INITED(WalletCreateAccountResponse,
    (int, Result, 0)
);

GRAFT_DEFINE_JSON_RPC_REQUEST(WalletCreateAccountResponseJsonRpc, WalletCreateAccountResponse)

GRAFT_DEFINE_IO_STRUCT(WalletCreateAccountCallbackRequest,
    (int,         Result),
    (std::string, WalletId),
    (std::string, Address),
    (std::string, ViewKey),
    (std::string, Account),
    (std::string, Seed)
);

GRAFT_DEFINE_JSON_RPC_REQUEST(WalletCreateAccountCallbackRequestJsonRpc, WalletCreateAccountCallbackRequest)

}

}
