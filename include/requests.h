#ifndef REQUESTS_H
#define REQUESTS_H

#include "router.h"

namespace graft {

void registerRTARequests(graft::Router &router);
void registerForwardRequests(graft::Router &router);
void registerHealthcheckRequests(graft::Router &router);
void registerDebugRequests(graft::Router &router);

}

#endif // REQUESTS_H

