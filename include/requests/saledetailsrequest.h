#ifndef SALEDETAILSREQUEST_H
#define SALEDETAILSREQUEST_H

#include "inout.h"
#include "router.h"

#include <vector>

namespace graft {


// fee per supernode
GRAFT_DEFINE_IO_STRUCT_INITED(SupernodeFee,
                       (std::string, address, ""),
                       (uint64_t, fee, 0)
                       );

GRAFT_DEFINE_IO_STRUCT_INITED(SaleDetailsRequest,
                             (std::string, PaymentID, ""),
                             (uint64_t, BlockNumber, 0)
                             );

GRAFT_DEFINE_IO_STRUCT(SaleDetailsResponse,
                       (std::vector<SupernodeFee>, AuthSample),
                       (std::string, Details)
                       );

void registerSaleDetailsRequest(graft::Router &router);

}

#endif // SALEDETAILSREQUEST_H
