#include "requests.h"
#include "requests/salerequest.h"
#include "requests/salestatusrequest.h"
#include "requests/rejectsalerequest.h"
#include "requests/getinforequest.h"

#include "requests/saledetailsrequest.h"
#include "requests/payrequest.h"
#include "requests/paystatusrequest.h"
#include "requests/rejectpayrequest.h"
#include "requests/forwardrequest.h"
#include "requests/healthcheckrequest.h"

#include "requests/sendrawtxrequest.h"
#include "requests/authorizertatxrequest.h"
#include "requests/sendsupernodeannouncerequest.h"


namespace graft {

void __registerDebugRequests(Router& router);

void registerRTARequests(graft::Router &router)
{
    graft::registerSaleRequest(router);
    graft::registerSaleStatusRequest(router);
    graft::registerRejectSaleRequest(router);
    graft::registerGetInfoRequest(router);
    graft::registerSaleDetailsRequest(router);
    graft::registerPayRequest(router);
    graft::registerPayStatusRequest(router);
    graft::registerRejectPayRequest(router);
    graft::registerAuthorizeRtaTxRequests(router);
    graft::registerSendSupernodeAnnounceRequest(router);
}

void registerForwardRequests(graft::Router &router)
{
    graft::registerForwardRequest(router);
}

void registerHealthcheckRequests(Router &router)
{
    graft::registerHealthcheckRequest(router);
}

void registerDebugRequests(Router &router)
{
    graft::__registerDebugRequests(router);
}

}
