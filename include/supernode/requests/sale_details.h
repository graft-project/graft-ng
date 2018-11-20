#ifndef SALEDETAILSREQUEST_H
#define SALEDETAILSREQUEST_H

#include "inout.h"
#include "router.h"

#include <vector>

namespace graft {


// fee per supernode
GRAFT_DEFINE_IO_STRUCT_INITED(SupernodeFee,
                       (std::string, Address, std::string()),
                       (std::string, Fee, std::string())
                       );

GRAFT_DEFINE_IO_STRUCT_INITED(SaleDetailsRequest,
                             (std::string, PaymentID, std::string()),
                             (uint64_t, BlockNumber, 0),
                             (std::string, callback_uri, std::string()) // this is for the case if supernode needs to ask remote supernode for details
                             );

GRAFT_DEFINE_IO_STRUCT(SaleDetailsResponse,
                       (std::vector<SupernodeFee>, AuthSample),
                       (std::string, Details)
                       );

void registerSaleDetailsRequest(graft::Router &router);

}

#endif // SALEDETAILSREQUEST_H
