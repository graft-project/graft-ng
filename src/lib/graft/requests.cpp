
#include "lib/graft/requests.h"
#include "lib/graft/requests/health_check.h"

namespace graft::request {

void registerHealthcheckRequests(Router &router)
{
    registerHealthcheckRequest(router);
}

}

