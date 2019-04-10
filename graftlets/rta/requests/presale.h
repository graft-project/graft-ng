
#pragma once

#include "lib/graft/router.h"
#include "lib/graft/jsonrpc.h"
#include "supernode/requestdefines.h"

namespace graft::supernode::request {

// presale request payload
GRAFT_DEFINE_IO_STRUCT_INITED(PresaleRequest,
    (std::string, PaymentID, std::string())
);

GRAFT_DEFINE_IO_STRUCT_INITED(PresaleResponse,
    (uint64, BlockNumber, 0),
    (std::string, BlockHash, std::string()),
    (std::vector<std::string>, AuthSample, std::vector<std::string>())
);

// presale request handler
Status handlePresaleRequest(const Router::vars_t& vars, const graft::Input& input, graft::Context& ctx, graft::Output& output);

}

