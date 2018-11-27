#pragma once

#include "lib/graft/router.h"
#include "lib/graft/jsonrpc.h"

namespace graft {

class Context;

namespace walletnode::request {

GRAFT_DEFINE_IO_STRUCT(WalletTransferDestination,
                       (std::string, Address),
                       (std::string, Amount)
                       );

GRAFT_DEFINE_IO_STRUCT(WalletPrepareTransferRequest,
    (std::string,                            WalletId),
    (std::string,                            Account),
    (std::string,                            Password),
    (std::vector<WalletTransferDestination>, Destinations)
);

GRAFT_DEFINE_JSON_RPC_REQUEST(WalletPrepareTransferRequestJsonRpc, WalletPrepareTransferRequest)

GRAFT_DEFINE_IO_STRUCT_INITED(WalletPrepareTransferResponse,
    (int, Result, 0)
);

GRAFT_DEFINE_JSON_RPC_REQUEST(WalletPrepareTransferJsonRpc, WalletPrepareTransferResponse)

GRAFT_DEFINE_IO_STRUCT(WalletPrepareTransferCallbackRequest,
    (int,                      Result),
    (std::string,              Fee),
    (std::vector<std::string>, Transactions)
);

GRAFT_DEFINE_JSON_RPC_REQUEST(WalletPrepareTransferCallbackRequestJsonRpc, WalletPrepareTransferCallbackRequest)

}

}
