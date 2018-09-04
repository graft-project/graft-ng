#pragma once

#include <string>
#include <def_io_struct.h>

namespace graft {

template<typename In, typename Out> class RouterT; class InHttp; class OutHttp; using Router = RouterT<InHttp, OutHttp>;

GRAFT_DEFINE_IO_STRUCT(RejectPayRequest,
    (std::string, PaymentID)
);

GRAFT_DEFINE_IO_STRUCT(RejectPayResponse,
    (int, Result)
);

void registerRejectPayRequest(graft::Router &router);

}

