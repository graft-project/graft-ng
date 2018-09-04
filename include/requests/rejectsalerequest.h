#pragma once

#include <string>
#include "inout.h"

namespace graft {

template<typename In, typename Out> class RouterT; class InHttp; class OutHttp; using Router = RouterT<InHttp, OutHttp>;

GRAFT_DEFINE_IO_STRUCT(RejectSaleRequest,
    (std::string, PaymentID)
);

GRAFT_DEFINE_IO_STRUCT(RejectSaleResponse,
    (int, Result)
);

void registerRejectSaleRequest(graft::Router &router);

}

