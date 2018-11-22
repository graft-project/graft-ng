#ifndef SENDTRANSFERREQUEST_H
#define SENDTRANSFERREQUEST_H

#include "router.h"
#include "jsonrpc.h"

namespace graft {

GRAFT_DEFINE_IO_STRUCT_INITED(SendTransferRequest,
    (std::vector<std::string>, Transactions, std::vector<std::string>())
);

GRAFT_DEFINE_IO_STRUCT_INITED(SendTransferResponse,
    (int, Result, 0)
);

void registerSendTransferRequest(graft::Router &router);

}

#endif // SENDTRANSFERREQUEST_H
