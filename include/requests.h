#ifndef REQUESTS_H
#define REQUESTS_H

#include "router.h"

namespace graft {

//forwards
class WalletManager;

void registerRTARequests(graft::Router &router);
void registerForwardRequests(graft::Router &router);
void registerHealthcheckRequests(graft::Router &router);
void registerDebugRequests(Router &router);
void registerWalletRequests(Router &router, WalletManager&);

}

#endif // REQUESTS_H
