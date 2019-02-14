
#pragma once

#include "lib/graft/router.h"

namespace graft::supernode::request {

void registerRTARequests(graft::Router &router);
void registerWalletApiRequests(graft::Router &router);
void registerForwardRequests(graft::Router &router);
void registerDebugRequests(Router &router);

}
