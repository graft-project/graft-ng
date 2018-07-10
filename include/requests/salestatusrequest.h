#ifndef SALESTATUSREQUEST_H
#define SALESTATUSREQUEST_H

#include "router.h"
#include "jsonrpc.h"

namespace graft {

GRAFT_DEFINE_IO_STRUCT(SaleStatusRequest,
    (std::string, PaymentID)
);

GRAFT_DEFINE_IO_STRUCT(SaleStatusResponse,
    (int, Status)
);



void registerSaleStatusRequest(graft::Router &router);

}

#endif // SALESTATUSREQUEST_H
