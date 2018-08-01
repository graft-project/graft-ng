#ifndef HEALTHCHECKREQUEST_H
#define HEALTHCHECKREQUEST_H

#include "router.h"

namespace graft {

GRAFT_DEFINE_IO_STRUCT(HealthcheckResponse,
    (std::string, NodeAccess)
);

void registerHealthcheckRequest(graft::Router &router);

}

#endif // HEALTHCHECKREQUEST_H
