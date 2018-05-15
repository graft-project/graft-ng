#ifndef PAYSTATUSREQUEST_H
#define PAYSTATUSREQUEST_H

#include "router.h"

namespace graft {

GRAFT_DEFINE_IO_STRUCT(PayStatusRequest,
    (std::string, PaymentID)
);

GRAFT_DEFINE_IO_STRUCT(PayStatusResponse,
    (int, Status)
);

void registerPayStatusRequest(graft::Router &router);

}

#endif // PAYSTATUSREQUEST_H
