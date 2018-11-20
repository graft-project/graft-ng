
#include "requests.h"
#include "supernode/requests/sale.h"
#include "supernode/requests/sale_status.h"
#include "supernode/requests/reject_sale.h"
#include "supernode/requests/get_info.h"

#include "supernode/requests/sale_details.h"
#include "supernode/requests/pay.h"
#include "supernode/requests/pay_status.h"
#include "supernode/requests/reject_pay.h"
#include "supernode/requests/forward.h"
#include "supernode/requests/health_check.h"

#include "supernode/requests/send_raw_tx.h"
#include "supernode/requests/authorize_rta_tx.h"
#include "supernode/requests/send_supernode_announce.h"

namespace graft {

void __registerDebugRequests(Router& router);
namespace supernode::system_info { void register_request(Router& router); }

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
    graft::requests::walletnode::registerForward(router);
}

void registerHealthcheckRequests(Router &router)
{
    graft::registerHealthcheckRequest(router);
}

void registerDebugRequests(Router &router)
{
    graft::__registerDebugRequests(router);
    graft::supernode::system_info::register_request(router);
}

}

