#ifndef REJECTSALEREQUEST_H
#define REJECTSALEREQUEST_H

#include "router.h"

namespace graft {

GRAFT_DEFINE_IO_STRUCT(RejectSaleRequest,
    (std::string, PaymentID)
);

GRAFT_DEFINE_IO_STRUCT(RejectSaleResponse,
    (int, Result)
);

void registerRejectSaleRequest(graft::Router &router);

}

#endif // REJECTSALEREQUEST_H
