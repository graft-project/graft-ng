
#pragma once

#include "lib/graft/router.h"
#include "lib/graft/jsonrpc.h"

namespace graft::supernode::request {

GRAFT_DEFINE_IO_STRUCT_INITED(SendTransferRequest,
    (std::vector<std::string>, Transactions, std::vector<std::string>())
);

GRAFT_DEFINE_IO_STRUCT_INITED(SendTransferResponse,
    (int, Result, 0)
);

void registerSendTransferRequest(graft::Router &router);

}

