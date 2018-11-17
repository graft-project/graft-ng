
#pragma once

#include "supernode/route/data.h"
#include "supernode/route/handler3.h"
#include "inout.h"

namespace graft::supernode::route {

struct JobParams
{
    Input input;
    Vars vars;
    Handler3 h3;
};

}





