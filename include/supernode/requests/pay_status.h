
#pragma once

#include "lib/graft/router.h"

namespace graft::supernode::request {

GRAFT_DEFINE_IO_STRUCT(PayStatusRequest,
    (std::string, PaymentID)
);

GRAFT_DEFINE_IO_STRUCT(PayStatusResponse,
    (int, Status)
);

void registerPayStatusRequest(graft::Router &router);

}

