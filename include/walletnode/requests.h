
#pragma once

#include "lib/graft/router.h"
#include "lib/graft/requests.h"

namespace graft::walletnode {

//forwards
class WalletManager;

namespace request {

void registerWalletRequests(Router &router, WalletManager&);

}

}
