#include "requests.h"
#include "salerequest.h"
#include "salestatusrequest.h"
#include "rejectsalerequest.h"
#include "getinforequest.h"

#include "saledetailsrequest.h"
#include "payrequest.h"
#include "paystatusrequest.h"
#include "rejectpayrequest.h"

#include "sendrawtxrequest.h"
#include "cryptonodecallbackrequest.h"
#include "txtosignrequest.h"


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
    graft::registerTxToSignRequest(router);
    graft::registerCryptonodeCallbacksRequest(router);

}

}
