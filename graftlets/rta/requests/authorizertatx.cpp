// Copyright (c) 2018, The Graft Project
//
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without modification, are
// permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice, this list of
//    conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright notice, this list
//    of conditions and the following disclaimer in the documentation and/or other
//    materials provided with the distribution.
//
// 3. Neither the name of the copyright holder nor the names of its contributors may be
//    used to endorse or promote products derived from this software without specific
//    prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
// EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
// THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
// STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
// THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include "authorizertatx.h"
#include "pay.h"
#include "sale.h"
#include "updatepaymentstatus.h"
#include "lib/graft/jsonrpc.h"
#include "common.h"
#include "supernode/requestdefines.h"

#include "supernode/requests/send_raw_tx.h"
#include "supernode/requests/broadcast.h"
#include "rta/supernode.h"
#include "rta/fullsupernodelist.h"
#include <misc_log_ex.h>
#include <exception>

// logging
#undef MONERO_DEFAULT_LOG_CATEGORY
#define MONERO_DEFAULT_LOG_CATEGORY "supernode.authorizertatx"

namespace {
    static const size_t RTA_VOTES_TO_REJECT  =  1/*2*/; // TODO: 1 and 3 while testing
    static const size_t RTA_VOTES_TO_APPROVE =  4/*7*/; //
    static const std::string RTA_TX_REQ_HANDLER_STATE_KEY = "rta_tx_req_handler_state";
    static const std::string RTA_TX_REQ_TX_KEY = "rta_tx_req_tx_key"; // key to store transaction
    static const std::string RTA_TX_REQ_TX_KEY_KEY = "rta_tx_req_tx_key_key"; // key to store transaction private key

    // reading transaction directly from context throws "bad_cast"
    void putTxToContext(const cryptonote::transaction &tx,  graft::Context &ctx)
    {
        cryptonote::blobdata txblob = cryptonote::tx_to_blob(tx);
        ctx.local[RTA_TX_REQ_TX_KEY] = txblob;
    }

    bool getTxFromContext(graft::Context &ctx, cryptonote::transaction &tx)
    {
        if (!ctx.local.hasKey(RTA_TX_REQ_TX_KEY)) {
            MWARNING("RTA_TX_REQ_TX_KEY not found in context");
            return false;
        }
        cryptonote::blobdata txblob = ctx.local[RTA_TX_REQ_TX_KEY];
        return cryptonote::parse_and_validate_tx_from_blob(txblob, tx);
    }

    void putTxKeyToContext(const crypto::secret_key &key, graft::Context &ctx)
    {
        ctx.local[RTA_TX_REQ_TX_KEY_KEY] = epee::string_tools::pod_to_hex(key);
    }

    bool getTxKeyFromContext(graft::Context &ctx, crypto::secret_key &key)
    {
        if (!ctx.local.hasKey(RTA_TX_REQ_TX_KEY_KEY)) {
            MWARNING("RTA_TX_REQ_TX_KEY_KEY not found in context");
            return false;
        }
        std::string tx_str = ctx.local[RTA_TX_REQ_TX_KEY_KEY];
        return epee::string_tools::hex_to_pod(tx_str, key);
    }

}

namespace graft::supernode::request {



enum class AuthorizeRtaTxRequestHandlerState : int {
    IncomingRequest = 0,    // incoming request from client, store it and return Status::Again
    RequestStored,          // request stored, waiting to be processed: check if already processed
    StatusUpdateSent,       // "payment status update" broadcast sent
    KeyImagesRequestSent,   // "is_key_image_spent" request sent to cryptonode;
    TxResposeSent           // "authorize_rta_tx_response" mullticast sent
};




enum class RtaTxState {
    Processing = 0,
    AlreadyProcessedApproved,
    AlreadyProcessedRejected
};


GRAFT_DEFINE_IO_STRUCT_INITED(SupernodeSignature,
                              (std::string, id_key, std::string()),
                              (std::string, result_signature, std::string()), // signarure for tx_id + result
                              (std::string, tx_signature, std::string())      // signature for tx_id only
                              );

GRAFT_DEFINE_IO_STRUCT_INITED(AuthorizeRtaTxRequestResponse,
                        (int, Result, STATUS_OK)
                       );


GRAFT_DEFINE_IO_STRUCT_INITED(AuthorizeRtaTxResponse,
                       (std::string, tx_id, std::string()),
                       (int, result, int(RTAAuthResult::Invalid)),
                       (SupernodeSignature, signature, SupernodeSignature())
                       );

GRAFT_DEFINE_IO_STRUCT_INITED(AuthorizeRtaTxResponseResponse,
                       (int, Result, STATUS_OK)
                       );

GRAFT_DEFINE_JSON_RPC_RESPONSE_RESULT(AuthorizeRtaTxRequestJsonRpcResponse, AuthorizeRtaTxRequestResponse);
GRAFT_DEFINE_JSON_RPC_RESPONSE_RESULT(AuthorizeRtaTxResponseJsonRpcResponse, AuthorizeRtaTxResponseResponse);


GRAFT_DEFINE_IO_STRUCT_INITED(IsKeyImageSpentRequest,
                              (std::vector<std::string>, key_images, std::vector<std::string>())
                              );

GRAFT_DEFINE_IO_STRUCT_INITED(IsKeyImageSpentResponse,
                              (std::vector<unsigned int>, spent_status, std::vector<unsigned int>()),
                              (std::string, status, std::string()),
                              (bool, untrusted, false)
                              );


int get_rta_key_index(const cryptonote::rta_header &rta_hdr, SupernodePtr supernode)
{
    int result = -1;
    for (size_t i = 0; i < rta_hdr.keys.size(); ++i) {
        if (rta_hdr.keys.at(i) == supernode->idKey()) {
            result = i;
            break;
        }
    }
    return result;
}


enum class RtaAuthResponseHandlerState : int {
    // Multicast call from cryptonode auth rta auth response
    RtaAuthReply = 0,
    // we pushed tx to tx pool, next is to broadcast status,
    TransactionPushReply,
    // Status broadcast reply
    StatusBroadcastReply
};



struct RtaAuthResult
{
    std::vector<SupernodeSignature> approved;
    std::vector<SupernodeSignature> rejected;
    bool alreadyApproved(const std::string &id)
    {
        return contains(approved, id);
    }

    bool alreadyRejected(const std::string &id)
    {
        return contains(rejected, id);
    }

private:
    bool contains(const std::vector<SupernodeSignature> &v, const std::string &id)
    {
        return std::find_if(v.begin(), v.end(), [&](const SupernodeSignature &item) {
            return item.id_key == id;
        }) != v.end();
    }
};




// TODO: this function duplicates PendingTransaction::putRtaSignatures
void putRtaSignaturesToTx(cryptonote::transaction &tx, const std::vector<SupernodeSignature> &signatures, bool testnet)
{
#if 0
    std::vector<cryptonote::rta_signature> bin_signs;
    for (const auto &sign : signatures) {
        cryptonote::rta_signature bin_sign;
        if (!cryptonote::get_account_address_from_str(bin_sign.address, testnet, sign.id_key)) {
            LOG_ERROR("error parsing address from string: " << sign.id_key);
            continue;
        }
        epee::string_tools::hex_to_pod(sign.tx_signature, bin_sign.signature);
        bin_signs.push_back(bin_sign);
    }
    tx.put_rta_signatures(bin_signs);
#endif
}


/*!
 * \brief signAuthResponse - signs RTA auth result
 * \param arg
 * \param supernode
 * \return
 */
//bool signAuthResponse(AuthorizeRtaTxResponse &arg, const SupernodePtr &supernode)
//{
//    crypto::signature sign;
//    supernode->signMessage(arg.tx_id + ":" + std::to_string(arg.result), sign);
//    arg.signature.result_signature = epee::string_tools::pod_to_hex(sign);
//    crypto::hash tx_id;
//    epee::string_tools::hex_to_pod(arg.tx_id, tx_id);
//    supernode->signHash(tx_id, sign);
//    arg.signature.tx_signature = epee::string_tools::pod_to_hex(sign);
//    arg.signature.id_key = supernode->idKeyAsString();
//    return true;
//}

/*!
 * \brief validateAuthResponse - validates (checks) RTA auth result signed by supernode
 * \param arg
 * \param supernode
 * \return
 */
bool validateAuthResponse(const AuthorizeRtaTxResponse &arg, const SupernodePtr &supernode)
{
    crypto::signature sign_result;
    crypto::signature sign_tx_id;
    crypto::hash tx_id;

    if (!epee::string_tools::hex_to_pod(arg.signature.result_signature, sign_result)) {
        LOG_ERROR("Error parsing signature: " << arg.signature.result_signature);
        return false;
    }

    if (!epee::string_tools::hex_to_pod(arg.signature.tx_signature, sign_tx_id)) {
        LOG_ERROR("Error parsing signature: " << arg.signature.result_signature);
        return false;
    }

    if (!epee::string_tools::hex_to_pod(arg.tx_id, tx_id)) {
        LOG_ERROR("Error parsing tx_id: " << arg.tx_id);
        return false;
    }

    std::string msg = arg.tx_id + ":" + std::to_string(arg.result);
    crypto::public_key id_key;
    epee::string_tools::hex_to_pod(arg.signature.id_key, id_key);
    bool r1 = supernode->verifySignature(msg, id_key, sign_result);
    bool r2 = supernode->verifyHash(tx_id, id_key, sign_tx_id);
    return r1 && r2;
}


bool isProxySupernode(const cryptonote::rta_header &rta_hdr, const SupernodePtr &supernode)
{
   return std::find(rta_hdr.keys.begin(), rta_hdr.keys.begin() + 3, supernode->idKey()) != rta_hdr.keys.begin() + 3;
}

bool isAuthSampleSupernode(const cryptonote::rta_header &rta_hdr, const SupernodePtr &supernode)
{
   return std::find(rta_hdr.keys.begin() + 3, rta_hdr.keys.end(), supernode->idKey()) != rta_hdr.keys.end();
}

bool isAuthSampleValid(const cryptonote::rta_header &rta_hdr, graft::Context &ctx)
{
    if (!ctx.global.hasKey(rta_hdr.payment_id + CONTEXT_KEY_PAYMENT_DATA)) {
        MERROR("Payment unknown: " << rta_hdr.payment_id);
        return false;
    }
    // TODO: implement me
    return true;
}


bool checkMyFee(const cryptonote::transaction &tx, const crypto::secret_key &tx_key, const cryptonote::rta_header &rta_hdr,
                 SupernodePtr supernode, graft::Context &ctx)
{
    if (!ctx.global.hasKey(rta_hdr.payment_id + CONTEXT_KEY_PAYMENT_DATA)) {
        MERROR("Payment unknown: " << rta_hdr.payment_id);
        return false;
    }

    // TODO implement me;
    return  true;
}




Status signRtaTxAndSendResponse(cryptonote::transaction &tx, const crypto::secret_key &tx_key, graft::Context& ctx, graft::Output &output)
{
    cryptonote::rta_header rta_hdr;
    if (!cryptonote::get_graft_rta_header_from_extra(tx, rta_hdr)) {
        MERROR("Failed to get rta_header from tx: " << cryptonote::get_transaction_hash(tx));
        return Status::Ok;
    }

    SupernodePtr supernode = ctx.global.get(CONTEXT_KEY_SUPERNODE, SupernodePtr());
    std::vector<cryptonote::rta_signature> rta_signatures(FullSupernodeList::AUTH_SAMPLE_SIZE + 3);
    int key_index = get_rta_key_index(rta_hdr, supernode);
    if (key_index < 0) {
        MERROR("Internal error: tx doesn't contain our key " << cryptonote::get_transaction_hash(tx));
        return Status::Ok;
    }

    cryptonote::rta_signature &rta_sign = rta_signatures.at(key_index);
    rta_sign.key_index = key_index;

    crypto::hash tx_hash = cryptonote::get_transaction_hash(tx);
    supernode->signHash(tx_hash, rta_sign.signature);

    cryptonote::add_graft_rta_signatures_to_extra2(tx.extra2, rta_signatures);

    PayRequest pay_req;
    utils::encryptTxToHex(tx, rta_hdr.keys, pay_req.TxBlob);
    utils::encryptTxKeyToHex(tx_key, rta_hdr.keys, pay_req.TxKey);
    std::vector<std::string> destinations;

    for (const auto & key : rta_hdr.keys) {
        destinations.push_back(epee::string_tools::pod_to_hex(key));
    }

    utils::buildBroadcastOutput(pay_req, supernode, destinations, "json_rpc/rta", "/core/authorize_rta_tx_response", output);
    ctx.local[RTA_TX_REQ_HANDLER_STATE_KEY] = AuthorizeRtaTxRequestHandlerState::TxResposeSent;
    // store payment id for a logging purposes
    ctx.local["payment_id"] = rta_hdr.payment_id;
    MDEBUG("calling authorize_rta_tx_response");
    return Status::Forward;

}

Status processNewRtaTx(cryptonote::transaction &tx, const crypto::secret_key &tx_key, graft::Context& ctx, graft::Output &output)
{
    cryptonote::rta_header rta_hdr;
    if (!cryptonote::get_graft_rta_header_from_extra(tx, rta_hdr)) {
        MERROR("Failed to get rta_header from tx: " << cryptonote::get_transaction_hash(tx));
        return Status::Ok;
    }

    SupernodePtr supernode = ctx.global.get(CONTEXT_KEY_SUPERNODE, SupernodePtr());

    // sanity check if we're proxy supernode or auth sample supernode
    if (!isProxySupernode(rta_hdr, supernode) && !isAuthSampleSupernode(rta_hdr, supernode)) {
        MERROR("Internal error: authorize_rta_tx_request handled by non-proxy non-auth-sample supernode: " << rta_hdr.payment_id);
        return Status::Ok;
    }

    if (!checkMyFee(tx, tx_key, rta_hdr, supernode, ctx)) {
        MERROR("Failed to validate supernode fee for payment: " << rta_hdr.payment_id);
        return Status::Ok;
    }

    // in case we're auth sample supernode
    if (isAuthSampleSupernode(rta_hdr, supernode)) {
        // check if auth sample valid
        if (!isAuthSampleValid(rta_hdr, ctx)) {
            MERROR("Invalid auth sample for payment: " << rta_hdr.payment_id);
            return Status::Ok;
        }
        // check if key images spent
        IsKeyImageSpentRequest req;
        for (const cryptonote::txin_v& tx_input : tx.vin) {
          if (tx_input.type() == typeid(cryptonote::txin_to_key)) {
              crypto::key_image k_image = boost::get<cryptonote::txin_to_key>(tx_input).k_image;
              req.key_images.push_back(epee::string_tools::pod_to_hex(k_image));
          }
        }
        output.path = "/is_key_image_spent";
        output.load(req);
        ctx.local[RTA_TX_REQ_HANDLER_STATE_KEY] = AuthorizeRtaTxRequestHandlerState::KeyImagesRequestSent;
        MDEBUG("sending /is_key_image_spent: " << output.data());
        return Status::Forward; // send request to cryptonode
    }

    return signRtaTxAndSendResponse(tx, tx_key, ctx, output);
}


Status processKeyImagesReply(const Router::vars_t& vars, const graft::Input& input,
                            graft::Context& ctx, graft::Output& output) noexcept
{
    IsKeyImageSpentResponse resp;
    if (!input.get(resp)) {
        MERROR("Failed to parse is_key_images_spent response");
        return Status::Ok;
    }

    if (resp.status != "OK") {
        MERROR("is_key_images_spent returned failure" << input.data());
        return Status::Ok;
    }

    for (const auto spent_status : resp.spent_status) {
        if (spent_status != 0) {
            MERROR("some of the key images spent, status: " << spent_status);
            return Status::Ok;
        }
    }

    cryptonote::transaction tx;
    if (!getTxFromContext(ctx, tx)) {
        MERROR("Failed to read tx from context");
        return Status::Ok;
    }

    crypto::secret_key tx_key;
    if (!getTxKeyFromContext(ctx, tx_key)) {
        MERROR("Failed to read tx_key from context");
        return Status::Ok;
    }
    // all good, multicast authorize_rta_tx_response
    MDEBUG("key images check passed, signing rta tx");
    return signRtaTxAndSendResponse(tx, tx_key, ctx, output);
}

Status storeRtaTxAndReplyOk(const Router::vars_t& vars, const graft::Input& input,
                            graft::Context& ctx, graft::Output& output) noexcept
{
    //  store input in local ctx.
    MDEBUG(__FUNCTION__ << " begins");


    BroadcastRequestJsonRpc reqjrpc;

    if (!input.get(reqjrpc)) {
        // cryptonode isn't supposed to handle any errors, it's job just to deliver a message
        MERROR("Failed to parse request");
        return sendOkResponseToCryptonode(output);
    }

    BroadcastRequest &req = reqjrpc.params;

    if (!utils::verifyBroadcastMessage(req, req.sender_address)) {
        MERROR("Failed to verify signature for message: " << input.data());
        return sendOkResponseToCryptonode(output);
    }

    // AuthorizeRtaTxRequest authReq;
    Input innerInput;
    innerInput.load(req.data);
    PayRequest tx_req;

    if (!innerInput.getT<serializer::JSON_B64>(tx_req)) {
        MERROR("Failed desirialize tx auth request: " << innerInput.data());
        return sendOkResponseToCryptonode(output);
    }

    MDEBUG("incoming tx auth request from: " << req.sender_address);
    ctx.local["request"] = tx_req;
    MDEBUG(__FUNCTION__ << " end");
    return sendAgainResponseToCryptonode(output);
}

/*!
 * \brief handleTxAuthRequestNew - handles RTA auth request multicasted over auth sample.
 * \param vars
 * \param input
 * \param ctx
 * \param output
 * \return
 */

Status handleTxAuthRequestNew(const Router::vars_t& vars, const graft::Input& /*input*/,
                            graft::Context& ctx, graft::Output& output) noexcept
{
    MDEBUG(__FUNCTION__ << " begin");
    assert(ctx.local.getLastStatus() == Status::Again);

    if (!ctx.local.hasKey("request")) {
        LOG_ERROR("Internal error. no input for 'again' status");
        return Status::Error;
    }

    SupernodePtr supernode = ctx.global.get(CONTEXT_KEY_SUPERNODE, SupernodePtr());
    PayRequest pay_req = ctx.local["request"];

    cryptonote::transaction tx;
    if (!utils::decryptTxFromHex(pay_req.TxBlob, supernode, tx)) {
        MERROR("Failed to decrypt tx from: " << pay_req.TxBlob);
        return Status::Ok;
    }

    crypto::secret_key tx_key;
   if (!utils::decryptTxKeyFromHex(pay_req.TxKey, supernode, tx_key)) {
       MERROR("Failed to decrypt tx key from: " << pay_req.TxKey);
       return Status::Ok;
   }


    crypto::hash tx_hash = cryptonote::get_transaction_hash(tx);
    cryptonote::rta_header rta_hdr;
    if (!cryptonote::get_graft_rta_header_from_extra(tx, rta_hdr)) {
        MERROR("Failed to read rta_header, non-rta tx: "<< tx_hash);
        return Status::Ok;
    }


    MDEBUG("incoming rta tx for payment: " << rta_hdr.payment_id);

    // check if payment id is known already (captured by "/core/store_payment_data" handler)
    if (!ctx.global.hasKey(rta_hdr.payment_id + CONTEXT_KEY_PAYMENT_DATA)) {
        MERROR("Payment id is unknown: " << rta_hdr.payment_id);
        return Status::Ok;
    }

    // here we might have a two cases - payment_id seen or new one


    // if new one:
    if (!ctx.global.hasKey(rta_hdr.payment_id + CONTEXT_KEY_RTA_TX_STATE)) { // new tx;
        // update payment status
        ctx.global.set(rta_hdr.payment_id + CONTEXT_KEY_RTA_TX_STATE, RtaTxState::Processing, SALE_TTL);
        UpdatePaymentStatusRequest req;
        req.PaymentID = rta_hdr.payment_id;
        req.Status = static_cast<int>(RTAStatus::InProgress);
        utils::buildBroadcastOutput(req, supernode, std::vector<std::string>(), "json_rpc/rta", "/core/update_payment_status", output);
        ctx.local[RTA_TX_REQ_HANDLER_STATE_KEY] = AuthorizeRtaTxRequestHandlerState::StatusUpdateSent;
        putTxToContext(tx, ctx);
        if (!getTxFromContext(ctx, tx)) {
            abort();
        }
        putTxKeyToContext(tx_key, ctx);
        MDEBUG("tx stored in local context for payment: " << rta_hdr.payment_id);

        return Status::Forward;
    } else {
        MDEBUG("payment id auth request already processed: " << rta_hdr.payment_id);
    }

    return Status::Ok;

}


Status handleTxAuthRequestStatusUpdated(const Router::vars_t& vars, const graft::Input& input,
                            graft::Context& ctx, graft::Output& output) noexcept
{
    BroadcastResponseFromCryptonodeJsonRpc resp;

    JsonRpcErrorResponse error;
    if (!input.get(resp) || resp.error.code != 0 || resp.result.status != STATUS_OK) {
        return  errorInternalError("Error multicasting payment status update", output);
    }

    cryptonote::transaction tx;
    if (!getTxFromContext(ctx, tx)) {
        MERROR("Failed to get tx from context");
        return Status::Ok;
    }

    crypto::secret_key tx_key;
    if (!getTxKeyFromContext(ctx, tx_key)) {
        MERROR("Failed to get tx key from context");
        return Status::Ok;
    }

    return processNewRtaTx(tx, tx_key, ctx, output);
}

/*!
 * \brief handleCryptonodeMulticastResponse - handles multicast response from cryptonode. Here we only interested in error checking,
 *                                            there's nothing in response except "ok"
 *
 * \param vars
 * \param input
 * \param ctx
 * \param output
 * \return
 */
Status handleCryptonodeReply(const Router::vars_t& vars, const graft::Input& input,
                            graft::Context& ctx, graft::Output& output)
{

    // check cryptonode reply
    MDEBUG(__FUNCTION__ << " begin");
    BroadcastResponseFromCryptonodeJsonRpc resp;

    JsonRpcErrorResponse error;
    if (!input.get(resp) || resp.error.code != 0 || resp.result.status != STATUS_OK) {
        return  errorInternalError("Error multicasting request", output);
    }
    std::string payment_id_local = ctx.local["payment_id"];
    MDEBUG("tx auth response multicast ask received for payment: " << payment_id_local);

    AuthorizeRtaTxRequestJsonRpcResponse out;
    out.result.Result = STATUS_OK;
    output.load(out);

    return Status::Ok;
}

/*!
 * \brief handleRtaAuthResponseMulticast - handles cryptonode/authorize_rta_tx_response call
 * \param vars
 * \param input
 * \param ctx
 * \param output
 * \return
 */
Status handleRtaAuthResponseMulticast(const Router::vars_t& vars, const graft::Input& input,
                            graft::Context& ctx, graft::Output& output)
{

    try {

        BroadcastRequestJsonRpc req;
        MDEBUG(__FUNCTION__ << " begin");

        if (!input.get(req)) { // can't parse request
            LOG_ERROR("failed to parse request: " + input.data());
            return errorCustomError(std::string("failed to parse request: ")  + input.data(), ERROR_INVALID_REQUEST, output);
        }

        // TODO: check if our address is listed in "receiver_addresses"
        AuthorizeRtaTxResponse rtaAuthResp;
        Input innerIn;

        innerIn.load(req.params.data);

        if (!innerIn.getT<serializer::JSON_B64>(rtaAuthResp)) {
            LOG_ERROR("error deserialize rta auth response");
            return errorInvalidParams(output);
        }

        SupernodePtr supernode = ctx.global.get(CONTEXT_KEY_SUPERNODE, SupernodePtr());

        RTAAuthResult result = static_cast<RTAAuthResult>(rtaAuthResp.result);
        // sanity check
        if (result != RTAAuthResult::Approved && result != RTAAuthResult::Rejected) {
            LOG_ERROR("Invalid rta auth result: " << rtaAuthResp.result);
            return errorInvalidParams(output);
        }


        std::string ctx_payment_id_key = rtaAuthResp.tx_id + CONTEXT_KEY_PAYMENT_ID_BY_TXID;

        if (!ctx.global.hasKey(ctx_payment_id_key)) {
            LOG_ERROR("no payment_id for tx: " << rtaAuthResp.tx_id);
            return errorCustomError(std::string("unknown tx: ") + rtaAuthResp.tx_id, ERROR_INTERNAL_ERROR, output);
        }
        std::string payment_id = ctx.global.get(ctx_payment_id_key, std::string());
        MDEBUG("incoming tx auth response payment: " << payment_id
                     << ", tx_id: " << rtaAuthResp.tx_id
                     << ", from: " << rtaAuthResp.signature.id_key
                     << ", result: " << int(result));

        // store payment id for a logging purposes
        ctx.local["payment_id"] = payment_id;

        // validate signature
        bool signOk = validateAuthResponse(rtaAuthResp, supernode);
        if (!signOk) {
            std::string msg = "failed to validate signature for rta auth response";
            LOG_ERROR(msg);
            return errorCustomError(msg,
                                    ERROR_RTA_SIGNATURE_FAILED,
                                    output);
        }
        // stop handling it if we already processed response
        RtaAuthResult authResult;
        std::string ctx_tx_to_auth_resp = rtaAuthResp.tx_id + CONTEXT_KEY_AUTH_RESULT_BY_TXID;
        if (ctx.global.hasKey(ctx_tx_to_auth_resp)) {
            authResult = ctx.global.get(ctx_tx_to_auth_resp, authResult);
        }

        if (authResult.alreadyApproved(rtaAuthResp.signature.id_key)
                || authResult.alreadyRejected(rtaAuthResp.signature.id_key)) {
            return errorCustomError(std::string("supernode: ") + rtaAuthResp.signature.id_key + " already processed",
                                    ERROR_ADDRESS_INVALID, output);
        }

        if (result == RTAAuthResult::Approved) {
            authResult.approved.push_back(rtaAuthResp.signature);
        } else {
            authResult.rejected.push_back(rtaAuthResp.signature);
        }

        MDEBUG("rta result accepted from " << rtaAuthResp.signature.id_key
               << ", payment: " << payment_id);

        // store result in context
        ctx.global.set(ctx_tx_to_auth_resp, authResult, RTA_TX_TTL);
        if (!ctx.global.hasKey(rtaAuthResp.tx_id + CONTEXT_KEY_AMOUNT_BY_TX_ID)) {
            std::string msg = std::string("no amount found for tx id: ") + rtaAuthResp.tx_id;
            LOG_ERROR(msg);
            return errorCustomError(msg, ERROR_INTERNAL_ERROR, output);
        }

        uint64_t tx_amount = ctx.global.get(rtaAuthResp.tx_id + CONTEXT_KEY_AMOUNT_BY_TX_ID, uint64_t(0));

        size_t rta_votes_to_approve = tx_amount / COIN > 100 ? 4 : 2;

        MDEBUG("approved votes: " << authResult.approved.size()
               << "/" << rta_votes_to_approve
               << ", rejected votes: " << authResult.rejected.size()
               << ", payment: " << payment_id);


        if (!ctx.global.hasKey(rtaAuthResp.tx_id + CONTEXT_KEY_TX_BY_TXID)) {
            std::string msg = std::string("rta auth response processed but no tx found for tx id: ") + rtaAuthResp.tx_id;
            LOG_ERROR(msg);
            return errorCustomError(msg, ERROR_INTERNAL_ERROR, output);
        }

        if (authResult.rejected.size() >= RTA_VOTES_TO_REJECT) {
            MDEBUG("payment: " << payment_id
                   << ", tx_id: " << rtaAuthResp.tx_id
                   << " rejected by auth sample, updating status");

            // tx rejected by auth sample, broadcast status;
            ctx.global[__FUNCTION__] = RtaAuthResponseHandlerState::StatusBroadcastReply;
            ctx.global.set(payment_id + CONTEXT_KEY_STATUS, static_cast<int> (RTAStatus::Fail), RTA_TX_TTL);
            buildBroadcastSaleStatusOutput(payment_id, static_cast<int> (RTAStatus::Fail), supernode, output);
            return Status::Forward;
        } else if (authResult.approved.size() >= rta_votes_to_approve) {
            MDEBUG("payment: " << payment_id
                   << ", tx_id: " << rtaAuthResp.tx_id
                   << " approved by auth sample, pushing tx to pool");

            SendRawTxRequest req;
            // store tx_id in local context so we can use it when broadcasting status
            ctx.local[CONTEXT_TX_ID] = rtaAuthResp.tx_id;
            cryptonote::transaction tx = ctx.global.get(rtaAuthResp.tx_id + CONTEXT_KEY_TX_BY_TXID, cryptonote::transaction());
            putRtaSignaturesToTx(tx, authResult.approved, supernode->testnet());
            createSendRawTxRequest(tx, req);
#if 0
            // kept for future debugging
            {
                MDEBUG("sending tx to cryptonode:  " << req.tx_as_hex);
                MDEBUG("  rta signatures: ");
                std::string buf;
                buf += "\n";
                for (const auto & rta_sign:  tx.rta_signatures) {
                    buf += string("      address: ") + cryptonote::get_account_address_as_str(true, rta_sign.address) + "\n";
                    buf += string("      signature: ") + epee::string_tools::pod_to_hex(rta_sign.signature) + "\n";
                }
                MDEBUG(buf);
            }
#endif

            // call cryptonode
            output.load(req);
            output.path = "/sendrawtransaction";
            ctx.local[__FUNCTION__] = RtaAuthResponseHandlerState::TransactionPushReply;

            return Status::Forward;
        } else {
            MDEBUG("not enough votes for approval/reject for payment: " << payment_id
                   << ", keep waiting for other votes");
            AuthorizeRtaTxResponseJsonRpcResponse out;
            out.result.Result = STATUS_OK;
            output.load(out);
            MDEBUG(__FUNCTION__ << " end");
            return Status::Ok;
        }


    } catch (const std::exception &e) {
        LOG_ERROR("std::exception  catched: " << e.what());
        return errorInternalError(std::string("exception in cryptonode/authorize_rta_tx_response handler: ") +  e.what(),
                                  output);
    } catch (...) {
        LOG_ERROR("unhandled exception");
        return errorInternalError(std::string("unknown exception in cryptonode/authorize_rta_tx_response handler"),
                                  output);
    }
}


// handles "/sendrawtransaction" response
Status handleCryptonodeTxPushResponse(const Router::vars_t& vars, const graft::Input& input,
                               graft::Context& ctx, graft::Output& output)
{

    MDEBUG(__FUNCTION__ << " begin for task: " << boost::uuids::to_string(ctx.getId()));
    std::string payment_id_local = ctx.local["payment_id"];
    MDEBUG("processing sendrawtransaction reply for payment: " << payment_id_local);

    SendRawTxResponse resp;
    // check if we have tx_id in local context
    std::string tx_id = ctx.local[CONTEXT_TX_ID];

    if (tx_id.empty()) {
        LOG_ERROR("internal erorr, tx_id key not found in local context");
        abort();
    }

    // obtain payment id for given tx_id
    std::string payment_id = ctx.global.get(tx_id + CONTEXT_KEY_PAYMENT_ID_BY_TXID, std::string());
    if (payment_id.empty()) {
        LOG_ERROR("Internal error, payment id not found for tx id: " << tx_id);
    }

    RTAStatus status = static_cast<RTAStatus>(ctx.global.get(payment_id + CONTEXT_KEY_STATUS, int(RTAStatus::None)));
    if (status == RTAStatus::None) {
        LOG_ERROR("can't find status for payment_id: " << payment_id);
        return errorInvalidParams(output);
    }


    if (!input.get(resp)) {
        LOG_ERROR("Failed to parse input: " << input.data());
        return errorInvalidParams(output);
    }

    if (status == RTAStatus::Success
            || status == RTAStatus::Fail
            || status == RTAStatus::RejectedByPOS
            || status == RTAStatus::RejectedByWallet) {
        MWARNING("payment: " << payment_id << ", most likely already processed,  status: " << int(status));
        return sendOkResponseToCryptonode(output);
    }

    if (resp.status != "OK") {
        // check for double spend
        if (resp.double_spend) { // specific cast, we can get cryptonode's /sendrawtransaction response before status broadcast,
                                 // just ignore it for now
            LOG_ERROR("double spend for payment: " << payment_id << ", tx: " << tx_id);
            return sendOkResponseToCryptonode(output);
        }
        status = RTAStatus::Fail;
        // LOG_ERROR("failed to put tx to pool: " << tx_id << ", reason: " << resp.reason);
        LOG_ERROR("failed to put tx to pool: " << tx_id << ", input: " << input.data());
    } else {
        status = RTAStatus::Success;
    }

    SupernodePtr supernode = ctx.global.get(CONTEXT_KEY_SUPERNODE, SupernodePtr());

    MDEBUG("broadcasting status for payment id: " << payment_id << ", status : " << int(status));
    buildBroadcastSaleStatusOutput(payment_id, int(status), supernode, output);
    ctx.local[__FUNCTION__] = RtaAuthResponseHandlerState::StatusBroadcastReply;
    MDEBUG(__FUNCTION__ << " end");
    return Status::Forward;
}

// handles status broadcast response, pass "ok" to caller (cryptonode)
Status handleStatusBroadcastResponse(const Router::vars_t& vars, const graft::Input& input,
                                     graft::Context& ctx, graft::Output& output)
{
    // TODO: check if cryptonode broadcasted status
    MDEBUG(__FUNCTION__ << " begin");
    std::string payment_id_local = ctx.local["payment_id"];
    MDEBUG("received status broadcasting result for payment: " << payment_id_local);
    BroadcastResponseFromCryptonodeJsonRpc in;
    JsonRpcErrorResponse error;
    if (!input.get(in) || in.error.code != 0 || in.result.status != STATUS_OK) {
        return errorCustomError("Error broadcasting status", ERROR_INTERNAL_ERROR, output);
    }


    // most likely cryptonode doesn't really care what we reply here
    AuthorizeRtaTxResponseJsonRpcResponse outResponse;
    outResponse.result.Result = STATUS_OK;
    output.load(outResponse);
    MDEBUG(__FUNCTION__ << " end");
    return Status::Ok;
}



/*!
 * \brief authorizeRtaTxRequestHandler - entry point of authorize_rta_tx handler
 * \param vars
 * \param input
 * \param ctx
 * \param output
 * \return
 */




/// 1. check if broadcast can be deserialized and signature matches
/// 2. decrypt the tx blob
/// 3. check if this rta request already processed by this node? if yes, we only need to check the quorum
/// 4. if this rta request wasn't processed before:
/// 3.1. check if tx already rejected
/// 3.2. check double spend key images
/// 3.3. validate tx amount
/// 3.4. if all checks passed, sigh the tx and put signature into rta_signatures to tx.extra2
/// 4. check if consensus of approval matches

Status handleAuthorizeRtaTxRequest(const Router::vars_t& vars, const graft::Input& input,
                                 graft::Context& ctx, graft::Output& output) noexcept
{

    try {
        AuthorizeRtaTxRequestHandlerState state = ctx.local.hasKey(RTA_TX_REQ_HANDLER_STATE_KEY) ? ctx.local[RTA_TX_REQ_HANDLER_STATE_KEY]
                                                                                                   : AuthorizeRtaTxRequestHandlerState::IncomingRequest;
        switch (state) {
        case AuthorizeRtaTxRequestHandlerState::IncomingRequest: //
            ctx.local[RTA_TX_REQ_HANDLER_STATE_KEY] = AuthorizeRtaTxRequestHandlerState::RequestStored;
            return storeRtaTxAndReplyOk(vars, input, ctx, output);
        case AuthorizeRtaTxRequestHandlerState::RequestStored:
            // ctx.local[__FUNCTION__] = HandlerState::CryptonodeReply;
            return handleTxAuthRequestNew(vars, input, ctx, output);
        case AuthorizeRtaTxRequestHandlerState::StatusUpdateSent: // status update broadcast sent;
            return handleTxAuthRequestStatusUpdated(vars, input, ctx, output);
        case AuthorizeRtaTxRequestHandlerState::KeyImagesRequestSent: // is_key_images_spent request sent;
            return processKeyImagesReply(vars, input, ctx, output);
        case AuthorizeRtaTxRequestHandlerState::TxResposeSent:
            return handleCryptonodeReply(vars, input, ctx, output);
        default: // internal error
            return errorInternalError(std::string("authorize_rta_tx_request: unhandled state: ") + std::to_string(int(state)),
                                      output);
        };
    } catch (const std::exception &e) {
        LOG_ERROR(__FUNCTION__ << " exception thrown: " << e.what());
    } catch (...) {
        LOG_ERROR(__FUNCTION__ << " unknown exception thrown");
    }
    return errorInternalError("exception thrown", output);

}

/*!
 * \brief authorizeRtaTxResponseHandler - handles supernode's RTA auth response multicasted over auth sample
 * \param vars
 * \param input
 * \param ctx
 * \param output
 * \return
 */
Status authorizeRtaTxResponseHandler(const Router::vars_t& vars, const graft::Input& input,
                                 graft::Context& ctx, graft::Output& output)
{


    try {
        RtaAuthResponseHandlerState state = ctx.local.hasKey(__FUNCTION__) ? ctx.local[__FUNCTION__] : RtaAuthResponseHandlerState::RtaAuthReply;
        MDEBUG(__FUNCTION__ << " state: " << int(state) << ", status: "<< (int) ctx.local.getLastStatus() << ", task id: " << boost::uuids::to_string(ctx.getId()));

        switch (state) {
        // actually not a reply, just incoming multicast. same as "called by client" and client is cryptonode here
        case RtaAuthResponseHandlerState::RtaAuthReply:
            ctx.local[__FUNCTION__] = RtaAuthResponseHandlerState::TransactionPushReply;
            return handleRtaAuthResponseMulticast(vars, input, ctx, output);

        case RtaAuthResponseHandlerState::TransactionPushReply:
            ctx.local[__FUNCTION__] = RtaAuthResponseHandlerState::StatusBroadcastReply;
            return handleCryptonodeTxPushResponse(vars, input, ctx, output);

        case RtaAuthResponseHandlerState::StatusBroadcastReply:
            return handleStatusBroadcastResponse(vars, input, ctx, output);
        default:
            LOG_ERROR("Internal error, unexpected state: " << (int)state);
            abort();
        };
    } catch (const std::exception &e) {
        LOG_ERROR(__FUNCTION__ << " exception thrown: " << e.what());
    } catch (...) {
        LOG_ERROR(__FUNCTION__ << " unknown exception thrown");
    }
    return errorInternalError("exception thrown", output);


}


}

