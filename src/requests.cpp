
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

namespace graft::supernode::request::debug { void __registerDebugRequests(Router& router); }
namespace graft::system_info { void register_request(Router& router); }

namespace graft::supernode::request {

void registerRTARequests(graft::Router &router)
{
    registerSaleRequest(router);
    registerSaleStatusRequest(router);
    registerRejectSaleRequest(router);
    registerGetInfoRequest(router);
    registerSaleDetailsRequest(router);
    registerPayRequest(router);
    registerPayStatusRequest(router);
    registerRejectPayRequest(router);
    registerAuthorizeRtaTxRequests(router);
    registerSendSupernodeAnnounceRequest(router);
}

void registerForwardRequests(graft::Router &router)
{
    walletnode::registerForwardRequest(router);
    walletnode::registerForward(router);
}

void registerHealthcheckRequests(Router &router)
{
    registerHealthcheckRequest(router);
}

void registerDebugRequests(Router &router)
{
    debug::__registerDebugRequests(router);
    graft::system_info::register_request(router);
}

}

