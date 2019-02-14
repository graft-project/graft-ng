#pragma once

#include "lib/graft/router.h"
#include "lib/graft/jsonrpc.h"

namespace graft {

class Context;

namespace walletnode::request {

GRAFT_DEFINE_IO_STRUCT(WalletBalanceRequest,
    (std::string, WalletId),
    (std::string, Account),
    (std::string, Password)
);

GRAFT_DEFINE_JSON_RPC_REQUEST(WalletBalanceRequestJsonRpc, WalletBalanceRequest)

GRAFT_DEFINE_IO_STRUCT_INITED(WalletBalanceResponse,
    (int, Result, 0)
);

GRAFT_DEFINE_JSON_RPC_REQUEST(WalletBalanceResponseJsonRpc, WalletBalanceResponse)

GRAFT_DEFINE_IO_STRUCT(WalletBalanceCallbackRequest,
    (int,         Result),
    (std::string, Balance),
    (std::string, UnlockedBalance)
);

GRAFT_DEFINE_JSON_RPC_REQUEST(WalletBalanceCallbackRequestJsonRpc, WalletBalanceCallbackRequest)

}

}
