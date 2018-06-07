#ifndef SALEDETAILSREQUEST_H
#define SALEDETAILSREQUEST_H

#include "router.h"

namespace graft {

GRAFT_DEFINE_IO_STRUCT(SaleDetailsRequest,
    (std::string, PaymentID)
);

GRAFT_DEFINE_IO_STRUCT(SaleDetailsResponse,
    (std::string, Details)
);

void registerSaleDetailsRequest(graft::Router &router);

}

#endif // SALEDETAILSREQUEST_H
