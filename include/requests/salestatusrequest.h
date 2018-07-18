#ifndef SALESTATUSREQUEST_H
#define SALESTATUSREQUEST_H

#include "router.h"
#include "jsonrpc.h"

namespace graft {

// returns sale status by sale id
GRAFT_DEFINE_IO_STRUCT(SaleStatusRequest,
    (std::string, PaymentID)
);



// message to be broadcasted
GRAFT_DEFINE_IO_STRUCT(UpdateSaleStatusBroadcast,
                       (std::string, PaymentID),
                       (int, Status),
                       (std::string, address),   // address who updates the status
                       (std::string, signature)  // signature who updates the status
                       );

GRAFT_DEFINE_IO_STRUCT(SaleStatusResponse,
    (int, Status)
);


void registerSaleStatusRequest(graft::Router &router);

}

#endif // SALESTATUSREQUEST_H
