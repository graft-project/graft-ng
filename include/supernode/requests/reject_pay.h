#ifndef REJECTPAYREQUEST_H
#define REJECTPAYREQUEST_H

#include "router.h"

namespace graft {

GRAFT_DEFINE_IO_STRUCT(RejectPayRequest,
    (std::string, PaymentID)
);

GRAFT_DEFINE_IO_STRUCT(RejectPayResponse,
    (int, Result)
);

void registerRejectPayRequest(graft::Router &router);

}

#endif // REJECTPAYREQUEST_H
