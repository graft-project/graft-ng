#include "requests.h"
#include "salerequest.h"

namespace graft {

void registerRTARequests(graft::Router &router)
{
    graft::registerSaleRequest(router);
}

}
