
#pragma once

#include "router.h"

namespace graft {

void registerForwardRequest(graft::Router& router);

namespace requests::walletnode {

void registerForward(graft::Router& router);

}

}

