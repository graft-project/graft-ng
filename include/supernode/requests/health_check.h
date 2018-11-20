
#pragma once

#include "router.h"

namespace graft {

GRAFT_DEFINE_IO_STRUCT(HealthcheckResponse,
    (std::string, NodeAccess)
);

void registerHealthcheckRequest(graft::Router& router);

}

