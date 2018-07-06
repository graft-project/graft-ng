#include "salerequest.h"
#include "requestdefines.h"
#include "requesttools.h"
#include "rta/supernode.h"
#include "rta/fullsupernodelist.h"

#include <string>

BOOST_HANA_ADAPT_STRUCT(graft::SaleData,
                        Address,
                        BlockNumber,
                        Amount
                        );

namespace graft {

// message to be multicasted to auth sample
GRAFT_DEFINE_IO_STRUCT(SaleDataMulticast,
                       (SaleData, sale_data),
                       (std::string, paymentId),
                       (int, status)
                       );

Status saleClientHandler(const Router::vars_t& vars, const graft::Input& input,
                         graft::Context& ctx, graft::Output& output)
{


    bool calledByClient = !ctx.local.hasKey(__FUNCTION__);
    ctx.local[__FUNCTION__] = true;

    JsonRpcError error;
    error.code = 0;

    // validate params, generate payment id, build auth sample, multicast to auth sample
    if (calledByClient) {
        SaleRequest in = input.get<SaleRequest>();
        do {
            if (in.Address.empty() && in.Amount.empty()) {
                return errorInvalidParams(output);            // store SaleData, payment_id and
            }
            std::string payment_id = in.PaymentID;
            if (payment_id.empty()) // request comes from POS.
            {
                payment_id = generatePaymentID();
            }

            uint64_t amount = convertAmount(in.Amount);
            if (amount <= 0)
            {
                return errorInvalidAmount(output);
            }

            if (!Supernode::validateAddress(in.Address, ctx.global.getConfig()->testnet))
            {
                error.code = ERROR_ADDRESS_INVALID;
                error.message = MESSAGE_ADDRESS_INVALID;
                break;
            }

            SupernodePtr supernode = ctx.global.get(CONTEXT_KEY_SUPERNODE, SupernodePtr());
            FullSupernodeListPtr fsl = ctx.global.get(CONTEXT_KEY_FULLSUPERNODELIST, FullSupernodeListPtr());

            SaleData data(in.Address, supernode->daemonHeight(), amount);

            // what needs to be multicasted to auth sample ?
            // 1. payment_id
            // 2. SaleData
            // 3. SaleStatus

            ctx.global[payment_id + CONTEXT_KEY_SALE] = data;
            ctx.global[payment_id + CONTEXT_KEY_STATUS] = static_cast<int>(RTAStatus::Waiting);

            if (!in.SaleDetails.empty())
            {
                ctx.global[payment_id + CONTEXT_KEY_SALE_DETAILS] = in.SaleDetails;
            }
            // generate auth sample
            std::vector<SupernodePtr> authSample;
            if (!fsl->buildAuthSample(data.BlockNumber, authSample)) {
                error.code = ERROR_INVALID_PARAMS;
                error.message  = "can't build auth sample"; // TODO: move to constants
                break;
            }
            // store SaleData, payment_id and status in local context, so when we got reply from cryptonode, we just pass it to client
            ctx.local["sale_data"]  = data;
            ctx.local["payment_id"]  = payment_id;

            // TODO: multicast sale to the auth sample nodes
            SaleDataMulticast sdm;
            sdm.paymentId = payment_id;
            sdm.sale_data = data;
            sdm.status = static_cast<int>(RTAStatus::Waiting);
            Output out;
            out.loadT<serializer::JSON_B64>(sdm);

            MulticastRequestJsonRpc cryptonode_req;

            for (const auto & sn : authSample) {
                cryptonode_req.params.addresses.push_back(sn->walletAddress());
            }

            cryptonode_req.params.callback_uri = "/dapi/v2.0/cryptonode/sale";
            cryptonode_req.params.data = out.data();
            output.load(cryptonode_req);

        } while (false);
        if (error.code != 0) {
            output.load(error);
            return Status::Error;
        }
        return Status::Forward;

    } else { // response from upstream, send reply to client
        SaleData data = ctx.local["sale_data"];
        string payment_id = ctx.local["payment_id"];
        SaleResponse out;
        out.BlockNumber = data.BlockNumber;
        out.PaymentID = payment_id;
        output.load(out);
        return Status::Ok;
    }



}

Status saleCryptonodeHandler(const Router::vars_t& vars, const graft::Input& input,
                             graft::Context& ctx, graft::Output& output)
{
    SaleRequest in = input.get<SaleRequest>();
    JsonRpcErrorResponse errorResponse;
    do {
        if (in.Address.empty() || in.Amount.empty()) {

        }

    } while (false);

    return Status::Ok;
}


void registerSaleRequest(graft::Router &router)
{
    Router::Handler3 h1(nullptr, saleClientHandler, nullptr);
    router.addRoute("/sale", METHOD_POST, h1);
    Router::Handler3 h2(nullptr, saleCryptonodeHandler, nullptr);
    router.addRoute("/cryptonode/sale", METHOD_POST, h2);
}

}
