
#include "supernode/requests/send_transfer.h"
#include "supernode/requests/send_raw_tx.h"
#include "supernode/requestdefines.h"

#undef MONERO_DEFAULT_LOG_CATEGORY
#define MONERO_DEFAULT_LOG_CATEGORY "supernode.sendtransferrequest"

namespace graft::supernode::request {

static const std::string scLastSendTxIndex("SendTransfer_txindex");
static const std::string scTxList("SendTransfer_txs");

enum class SendTransferHandlerState : int
{
    ClientRequest = 0,
    TxStatusReply
};

Status handleClientSendRequest(const Router::vars_t& vars, const graft::Input& input,
                               graft::Context& ctx, graft::Output& output)
{

    MDEBUG(__FUNCTION__ << " begin");
    SendTransferRequest req;

    if (!input.get(req))
    {
        return errorInvalidParams(output);
    }

    if (req.Transactions.empty())
    {
        return errorInvalidTransaction("tx empty", output);
    }

    for (auto it = req.Transactions.cbegin(); it != req.Transactions.cend(); ++it)
    {
        std::string tx_hex = *it;
        // parse tx and validate tx, read tx id
        cryptonote::transaction tx;
        crypto::hash tx_hash, tx_prefix_hash;
        cryptonote::blobdata tx_blob;
        if (!epee::string_tools::parse_hexstr_to_binbuff(tx_hex, tx_blob))
        {
            return errorInvalidTransaction(tx_hex, output);
        }
        if (!cryptonote::parse_and_validate_tx_from_blob(tx_blob, tx, tx_hash, tx_prefix_hash))
        {
            return errorInvalidTransaction(tx_hex, output);
        }

        MDEBUG("processing send transfer, transfer:  "
               << ", tx_id: " <<  epee::string_tools::pod_to_hex(tx_hash));
    }

    ctx.local[scLastSendTxIndex] = 1;
    ctx.local[scTxList] = req.Transactions;

    // call cryptonode
    SendRawTxRequest request;
    request.tx_as_hex = req.Transactions[0];
    output.load(request);
    output.path = "/sendrawtransaction";
    return Status::Forward;
}

// handles response from cryptonode/rta/multicast call with tx auth request
Status handleTxStatusReply(const Router::vars_t& vars, const graft::Input& input,
                           graft::Context& ctx, graft::Output& output)
{
    // check cryptonode reply
    MDEBUG(__FUNCTION__ << " begin");
    std::vector<std::string> txs = ctx.local[scTxList];
    unsigned int tx_index = ctx.local[scLastSendTxIndex];
    if (tx_index < txs.size())
    {
        // call cryptonode
        SendRawTxRequest request;
        request.tx_as_hex = txs[tx_index];
        output.load(request);
        output.path = "/sendrawtransaction";
        ctx.local[scLastSendTxIndex] = tx_index + 1;
        return Status::Forward;
    }
    SendTransferResponse out;
    out.Result = STATUS_OK;
    output.load(out);
    return Status::Ok;
}

/*!
 * \brief sendTransferHandler - handles "/walletapi/v2.0/send_transfer" request
 * \param vars
 * \param input
 * \param ctx
 * \param output
 * \return
 */
Status sendTransferHandler(const Router::vars_t& vars, const graft::Input& input,
                           graft::Context& ctx, graft::Output& output)
{
    SendTransferHandlerState state = ctx.local.hasKey(__FUNCTION__)
            ? ctx.local[__FUNCTION__] : SendTransferHandlerState::ClientRequest;
    // state machine to perform two calls to cryptonode and return result to the client
    switch (state) {
    // client requested "/send_transfer"
    case SendTransferHandlerState::ClientRequest:
        ctx.local[__FUNCTION__] = SendTransferHandlerState::TxStatusReply;
        // "handleClientSendRequest" returns Forward;
        return handleClientSendRequest(vars, input, ctx, output);
    case SendTransferHandlerState::TxStatusReply:
        // check status if transaction sending and answer to client
        return handleTxStatusReply(vars, input, ctx, output);
     default:
        LOG_ERROR("Internal error: unhandled state");
        abort();
    };
}

void registerSendTransferRequest(Router &router)
{
    Router::Handler3 clientHandler(nullptr, sendTransferHandler, nullptr);
    router.addRoute("/send_transfer", METHOD_POST, clientHandler);
}

}
