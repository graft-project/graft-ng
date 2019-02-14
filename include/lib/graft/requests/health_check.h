
#pragma once

#include "lib/graft/router.h"

namespace graft::request {

GRAFT_DEFINE_IO_STRUCT(HealthcheckResponse,
    (std::string, NodeAccess)
);

void registerHealthcheckRequest(graft::Router& router);

}

