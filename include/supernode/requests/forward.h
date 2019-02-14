
#pragma once

#include "lib/graft/router.h"

namespace graft::supernode::request::walletnode {

void registerForwardRequest(graft::Router& router);

void registerForward(graft::Router& router);

}

