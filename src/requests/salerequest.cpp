#include "salerequest.h"
#include "requestdefines.h"
#include "requesttools.h"
#include "rta/supernode.h"
#include "rta/fullsupernodelist.h"
#include "requests/multicast.h"
#include "requests/broadcast.h"
#include "requests/salestatusrequest.h"

#include <string>

BOOST_HANA_ADAPT_STRUCT(graft::SaleData,
                        Address,
                        BlockNumber,
                        Amount
                        );

namespace graft {

static const std::chrono::seconds SALE_TTL = std::chrono::seconds(60);

// message to be multicasted to auth sample
GRAFT_DEFINE_IO_STRUCT(SaleDataMulticast,
                       (SaleData, sale_data),
                       (std::string, paymentId),
                       (int, status),
                       (string, details)
                       );


// message to be broadcasted
GRAFT_DEFINE_IO_STRUCT(UpdateSaleStatusBroadcast,
                       (std::string, PaymentID),
                       (int, Status),
                       (std::string, address),   // address who updates the status
                       (std::string, signature)  // signature who updates the status
                       );


Status broadcastSaleStatus(graft::Context &ctx, graft::Output &output, SupernodePtr supernode, const string &PaymentID, int status)
{
    UpdateSaleStatusBroadcast ussb;
    ussb.address = supernode->walletAddress();
    ussb.Status = status;
    ussb.PaymentID = PaymentID;


    // compose signature
    std::string msg = PaymentID + ":" + to_string(status);
    crypto::signature sign;
    supernode->signMessage(msg, sign);
    ussb.signature = epee::string_tools::pod_to_hex(sign);

    // send payload
    BroadcastRequestJsonRpc req;
    req.method = "broadcast";
    // req.

    return Status::Ok;
}



/*!
 * \brief saleClientHandler - handles /dapi/v2.0/sale POS request
 * \param vars
 * \param input
 * \param ctx
 * \param output
 * \return
 */
Status saleClientHandler(const Router::vars_t& vars, const graft::Input& input,
                         graft::Context& ctx, graft::Output& output)
{

    bool calledByClient = !ctx.local.hasKey(__FUNCTION__);
    ctx.local[__FUNCTION__] = true;

    JsonRpcError error;
    error.code = 0;

    // validate params, generate payment id, build auth sample, multicast to auth sample
    if (calledByClient) {
        SaleRequestJsonRpc req;
        if (!input.get(req)) {
            return errorInvalidParams(output);
        }

        const SaleRequest &in = req.params;
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

            // reply to caller (POS)
            SaleData data(in.Address, supernode->daemonHeight(), amount);

            // what needs to be multicasted to auth sample ?
            // 1. payment_id
            // 2. SaleData
            if (!in.SaleDetails.empty())
            {
                ctx.global.set(payment_id + CONTEXT_KEY_SALE_DETAILS, in.SaleDetails, SALE_TTL);
            }


            // generate auth sample
            std::vector<SupernodePtr> authSample;
            if (!fsl->buildAuthSample(data.BlockNumber, authSample) || authSample.size() != FullSupernodeList::AUTH_SAMPLE_SIZE) {
                error.code = ERROR_INVALID_PARAMS;
                error.message  = MESSAGE_RTA_CANT_BUILD_AUTH_SAMPLE;
                break;
            }


            // here we need to perform two actions:
            // 1. multicast sale over auth sample
            // 2. broadcast sale status
            ctx.global.set(payment_id + CONTEXT_KEY_SALE, data, SALE_TTL);
            ctx.global.set(payment_id + CONTEXT_KEY_STATUS, static_cast<int>(RTAStatus::Waiting), SALE_TTL);

            // store SaleData, payment_id and status in local context, so when we got reply from cryptonode, we just pass it to client
            ctx.local["sale_data"]  = data;
            ctx.local["payment_id"]  = payment_id;

            // multicast sale to the auth sample nodes
            SaleDataMulticast sdm;
            sdm.paymentId = payment_id;
            sdm.sale_data = data;
            sdm.status = static_cast<int>(RTAStatus::Waiting);
            sdm.details = in.SaleDetails;
            Output out;
            out.loadT<serializer::JSON_B64>(sdm);

            MulticastRequestJsonRpc cryptonode_req;

            for (const auto & sn : authSample) {
                cryptonode_req.params.receiver_addresses.push_back(sn->walletAddress());
            }

            cryptonode_req.method = "multicast";
            cryptonode_req.params.callback_uri =  "/cryptonode/sale"; // "/method" appended on cryptonode side
            cryptonode_req.params.data = out.data();
            output.load(cryptonode_req);
            output.uri = ctx.global.getConfig()->cryptonode_rpc_address + "/json_rpc/rta";
            LOG_PRINT_L0("calling: " << output.uri);
            LOG_PRINT_L0("data: " << output.data());

        } while (false);

        if (error.code != 0) {
            output.load(error);
            return Status::Error;
        }

        return Status::Forward;

    } else { // response from upstream, send reply to client (POS)
        LOG_PRINT_L0("response from cryptonode: " << input.data());
        LOG_PRINT_L0("status: " << (int)ctx.local.getLastStatus());
        // TODO: check cryptonode reply
        MulticastResponseFromCryptonodeJsonRpc resp;
        if (!input.get(resp) || resp.error.code != 0 || resp.result.status != STATUS_OK) {
            error.code = ERROR_INTERNAL_ERROR;
            error.message = "Error multicasting request";
            output.load(error);
            return Status::Error;
        }

        SaleData data = ctx.local["sale_data"];
        string payment_id = ctx.local["payment_id"];
        SaleResponseJsonRpc out;
        out.result.BlockNumber = data.BlockNumber;
        out.result.PaymentID = payment_id;
        output.load(out);
        return Status::Ok;
    }
}

/*!
 * \brief saleCryptonodeHandler - handles /dapi/v2.0/cryptonode/sale - call coming from cryptonode (multicasted), we just need to store "sale_data"
 * \param vars
 * \param input
 * \param ctx
 * \param output
 * \return
 */
Status saleCryptonodeHandler(const Router::vars_t& vars, const graft::Input& input,
                             graft::Context& ctx, graft::Output& output)
{

    MulticastRequestJsonRpc req;
    if (!input.get(req)) {
        return errorInvalidParams(output);
    }


    JsonRpcError error;
    error.code = 0;

    SaleDataMulticast sdm;

    graft::Input innerInput;
    innerInput.load(req.params.data);

    LOG_PRINT_L0("input loaded");
    if (!innerInput.getT<serializer::JSON_B64>(sdm)) {
        return errorInvalidParams(output);
    }
    const std::string &payment_id = sdm.paymentId;
    LOG_PRINT_L0("sale details received from multicast: " << sdm.details);
    // TODO: should be signed by sender??
    if (!ctx.global.hasKey(payment_id + CONTEXT_KEY_SALE)) {
        ctx.global[payment_id + CONTEXT_KEY_SALE] = sdm.sale_data;
        ctx.global[payment_id + CONTEXT_KEY_STATUS] = sdm.status;
        ctx.global[payment_id + CONTEXT_KEY_SALE_DETAILS] = sdm.details;
    } else {
        LOG_PRINT_L0("payment " << payment_id << " already known");
    }


    MulticastResponseToCryptonodeJsonRpc resp;
    resp.result.status = "OK";
    output.load(resp);

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
