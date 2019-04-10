
#include "supernode/requests/presale.h"
#include "supernode/requests/multicast.h"
#include "supernode/requests/broadcast.h"
#include "supernode/requests/sale_status.h"

#include "supernode/requestdefines.h"
#include "lib/graft/requesttools.h"
#include "rta/supernode.h"
#include "rta/fullsupernodelist.h"

#include <string>

#undef MONERO_DEFAULT_LOG_CATEGORY
#define MONERO_DEFAULT_LOG_CATEGORY "supernode.presalerequest"

namespace graft::supernode::request {


Status handlePresaleRequest(const Router::vars_t& vars, const graft::Input& input, graft::Context& ctx, graft::Output& output)
{
    PresaleRequest req;
    PresaleResponse resp;

    if (!input.get(req)) {
        return errorInvalidParams(output);
    }

    if (req.PaymentID.length() == 0) {
        return errorInvalidPaymentID(output);
    }

    SupernodePtr supernode = ctx.global.get(CONTEXT_KEY_SUPERNODE, SupernodePtr());
    FullSupernodeListPtr fsl = ctx.global.get(CONTEXT_KEY_FULLSUPERNODELIST, FullSupernodeListPtr());


    std::vector<SupernodePtr> sample;

    if (!fsl->buildAuthSample(fsl->getBlockchainBasedListMaxBlockNumber(), req.PaymentID, sample, resp.BlockNumber)) {
        return errorCustomError(MESSAGE_RTA_CANT_BUILD_AUTH_SAMPLE, ERROR_INVALID_PARAMS, output);
    }

    if (!fsl->getBlockHash(resp.BlockNumber, resp.BlockHash)) {
        return errorInternalError("failed to get block hash", output);
    }

    for (auto& sPtr : sample) {
        resp.AuthSample.push_back(sPtr->idKeyAsString());
    }

    output.load(resp);

    return Status::Ok;


}





void registerSaleRequest(graft::Router &router)
{
    Router::Handler3 h1(nullptr, handlePresaleRequest, nullptr);
    router.addRoute("/presale", METHOD_POST, h1);
}

}

