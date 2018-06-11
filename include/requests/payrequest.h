#ifndef PAYREQUEST_H
#define PAYREQUEST_H

#include "router.h"

namespace graft {

GRAFT_DEFINE_IO_STRUCT(PayRequest,
    (std::string, PaymentID),
    (std::string, Address),
    (uint64, BlockNumber), //TODO: Need to check if we really need it.
    (std::string, Amount)
);

GRAFT_DEFINE_IO_STRUCT(PayResponse,
    (int, Result)
);

void registerPayRequest(graft::Router &router);

}

#endif // PAYREQUEST_H
