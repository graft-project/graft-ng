
#pragma once

#include "lib/graft/router.h"

namespace graft::supernode::request::walletnode {

void registerCryptonodeForward(graft::Router& router);
void registerWalletnodeForward(graft::Router& router);
void registerLegacySupernodeForward(graft::Router& router);

}

