
#pragma once

#include "lib/graft/router.h"

namespace graft::supernode::request {

void registerHealthcheckRequests(graft::Router &router);

}

namespace graft::walletnode {

//forwards
class WalletManager;

namespace request {

void registerWalletRequests(Router &router, WalletManager&);

}

}
