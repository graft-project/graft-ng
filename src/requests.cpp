#include "requests.h"
#include "salerequest.h"
#include "salestatusrequest.h"
#include "rejectsalerequest.h"
#include "getinforequest.h"

#include "saledetailsrequest.h"
#include "payrequest.h"
#include "paystatusrequest.h"
#include "rejectpayrequest.h"
#include "forwardrequest.h"
#include "healthcheckrequest.h"

#include "sendrawtxrequest.h"
#include "authorizertatxrequest.h"
#include "sendsupernodeannouncerequest.h"

#include "requests/system_info.h"


namespace graft {

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
    graft::supernode::request::system_info::register_request(router);
}

}
