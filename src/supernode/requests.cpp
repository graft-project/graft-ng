
#include "supernode/requests/sale.h"
#include "supernode/requests/sale_status.h"
#include "supernode/requests/reject_sale.h"
#include "supernode/requests/get_info.h"

#include "supernode/requests/sale_details.h"
#include "supernode/requests/pay.h"
#include "supernode/requests/pay_status.h"
#include "supernode/requests/reject_pay.h"
#include "supernode/requests/send_transfer.h"
#include "supernode/requests/forward.h"
#include "lib/graft/requests/health_check.h"

#include "supernode/requests/send_raw_tx.h"
#include "supernode/requests/authorize_rta_tx.h"
#include "supernode/requests/send_supernode_announce.h"
#include "supernode/requests/send_supernode_stake_txs.h"
#include "supernode/requests/blockchain_based_list.h"

namespace graft::supernode::request::debug { void __registerDebugRequests(Router& router); }
namespace graft::request::system_info { void register_request(Router& router); }

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
    registerSendSupernodeStakeTransactionsRequest(router);
    registerBlockchainBasedListRequest(router);
}

void registerWalletApiRequests(graft::Router &router)
{
    registerSendTransferRequest(router);
}

void registerForwardRequests(graft::Router &router)
{
    walletnode::registerForwardRequest(router);
    walletnode::registerForward(router);
}

void registerDebugRequests(Router &router)
{
    debug::__registerDebugRequests(router);
    graft::request::system_info::register_request(router);
}

}

