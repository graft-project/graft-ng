
#pragma once

#include "common.h"
#include "lib/graft/router.h"
#include "lib/graft/jsonrpc.h"
#include "supernode/requestdefines.h"


namespace graft::supernode::request {




// Sale request payload
GRAFT_DEFINE_IO_STRUCT_INITED(SaleRequest,
    (std::string, PaymentID, std::string()), // payment id
    (PaymentData, paymentData, PaymentData()) // encrypted payment data (incl amount)
);

// shared constants
extern const std::chrono::seconds SALE_TTL;

Status handleSaleRequest(const Router::vars_t& vars, const graft::Input& input,
                         graft::Context& ctx, graft::Output& output);

// void registerSaleRequest(graft::Router &router);

}

