#include "requests.h"
#include "salerequest.h"
#include "salestatusrequest.h"
#include "rejectsalerequest.h"
#include "saledetailsrequest.h"
#include "payrequest.h"
#include "paystatusrequest.h"
#include "rejectpayrequest.h"

namespace graft {

void registerRTARequests(graft::Router &router)
{
    graft::registerSaleRequest(router);
    graft::registerSaleStatusRequest(router);
    graft::registerRejectSaleRequest(router);
    graft::registerSaleDetailsRequest(router);
    graft::registerPayRequest(router);
    graft::registerPayStatusRequest(router);
    graft::registerRejectPayRequest(router);
}

}
