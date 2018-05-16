#include "requests.h"
#include "salerequest.h"
#include "salestatusrequest.h"
#include "rejectsalerequest.h"
#include "getinforequest.h"

namespace graft {

void registerRTARequests(graft::Router &router)
{
    graft::registerSaleRequest(router);
    graft::registerSaleStatusRequest(router);
    graft::registerRejectSaleRequest(router);
    graft::registerGetInfoRequest(router);
}

}
