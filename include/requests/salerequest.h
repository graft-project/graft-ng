#ifndef SALEREQUEST_H
#define SALEREQUEST_H

#include "router.h"

namespace graft {

GRAFT_DEFINE_IO_STRUCT(SaleRequest,
    (std::string, Address),
    (std::string, SaleDetails),
    (std::string, Amount)
);

GRAFT_DEFINE_IO_STRUCT(SaleResponse,
    (uint64, BlockNumber), //TODO: Need to check if we really need it.
    (std::string, PaymentID)
);

void registerSaleRequest(graft::Router &router);

}

#endif // SALEREQUEST_H
