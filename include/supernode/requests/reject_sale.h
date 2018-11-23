
#pragma once

#include "lib/graft/router.h"

namespace graft::supernode::request {

GRAFT_DEFINE_IO_STRUCT(RejectSaleRequest,
    (std::string, PaymentID)
);

GRAFT_DEFINE_IO_STRUCT(RejectSaleResponse,
    (int, Result)
);

void registerRejectSaleRequest(graft::Router &router);

}

