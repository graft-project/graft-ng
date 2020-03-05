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
#include "send_raw_tx.h"
#include "lib/graft/jsonrpc.h"
#include "supernode/requests/common.h"
#include "supernode/requestdefines.h"

#include "supernode/requests/broadcast.h"
#include "rta/supernode.h"
#include "rta/fullsupernodelist.h"
#include <utils/utils.h>
#include <misc_log_ex.h>
#include <syncobj.h>

#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <exception>


// logging
#undef MONERO_DEFAULT_LOG_CATEGORY
#define MONERO_DEFAULT_LOG_CATEGORY "supernode.authorizertatx"

namespace {
    static const size_t RTA_VOTES_TO_REJECT  =  1/*2*/; // TODO: 1 and 3 while testing
    static const size_t RTA_VOTES_TO_APPROVE =  6/*7*/; //


    epee::critical_section vote_lock;

    // reading transaction directly from context throws "bad_cast"
    void putTxToLocalContext(const cryptonote::transaction &tx,  graft::Context &ctx, const std::string &key)
    {
        cryptonote::blobdata txblob = cryptonote::tx_to_blob(tx);
        ctx.local[key] = txblob;
    }

    bool getTxFromLocalContext(graft::Context &ctx, cryptonote::transaction &tx, const std::string &key)
    {
        if (!ctx.local.hasKey(key)) {
            MWARNING("key not found in context: " << key);
            return false;
        }
        cryptonote::blobdata txblob = ctx.local[key];
        return cryptonote::parse_and_validate_tx_from_blob(txblob, tx);
    }


    void dumpSignatures(const cryptonote::transaction &tx)
    {
        cryptonote::rta_header rta_hdr;
        if (!cryptonote::get_graft_rta_header_from_extra(tx, rta_hdr)) {
            MERROR("failed to read rta_header from tx: " << cryptonote::get_transaction_hash(tx));
            return ;
        }
        std::vector<cryptonote::rta_signature> rta_signs;
        if (!cryptonote::get_graft_rta_signatures_from_extra2(tx, rta_signs)) {
            MERROR("failed to read rta_signatures from tx: " << cryptonote::get_transaction_hash(tx));
            return;
        }
        for (int i = 0; i < rta_signs.size(); ++i) {
            MDEBUG(rta_hdr.keys.at(rta_signs.at(i).key_index) << ":" << rta_signs.at(i).signature);
        }
    }

    bool mergeTxSignatures(const cryptonote::transaction &src, cryptonote::transaction &dst)
    {
        crypto::hash tx_hash = cryptonote::get_transaction_hash(src);

        if (tx_hash != cryptonote::get_transaction_hash(dst)) {
            MERROR("merging different transactions");
            return false;
        }

        cryptonote::rta_header rta_hdr_src, rta_hdr_dst;
        if (!cryptonote::get_graft_rta_header_from_extra(src, rta_hdr_src)) {
            MERROR("failed to read rta_header from tx: " << cryptonote::get_transaction_hash(src));
            return false;
        }

        if (!cryptonote::get_graft_rta_header_from_extra(dst, rta_hdr_dst)) {
            MERROR("failed to read rta_header from tx: " << cryptonote::get_transaction_hash(dst));
            return false;
        }

        if (!(rta_hdr_dst == rta_hdr_src)) {
            MERROR("source and destination rta_headers are different");
            return false;
        }

        std::vector<cryptonote::rta_signature> rta_signs_src, rta_signs_dst;
        if (!cryptonote::get_graft_rta_signatures_from_extra2(src, rta_signs_src)) {
            MERROR("failed to read rta_signatures from tx: " << cryptonote::get_transaction_hash(src));
            return false;
        }

        if (!cryptonote::get_graft_rta_signatures_from_extra2(dst, rta_signs_dst)) {
            MERROR("failed to read rta_signatures from tx: " << cryptonote::get_transaction_hash(dst));
            return false;
        }

        // signatures container resized alreadt, so the size should be the same
        if (rta_signs_dst.size() != rta_signs_src.size()) {
            MERROR("source and destination rta_signatures size are different");
            return false;
        }

        if (rta_hdr_dst.keys.size() != rta_signs_dst.size()) {
            MERROR("destination transaction keys amount and signatures amount mismatches");
            return false;
        }

        if (rta_hdr_src.keys.size() != rta_signs_src.size()) {
            MERROR("destination transaction keys amount and signatures amount mismatches");
            return false;
        }

        for (size_t i = 0; i < rta_signs_dst.size(); ++i) {
            if (!graft::Supernode::verifyHash(tx_hash, rta_hdr_src.keys.at(i), rta_signs_dst.at(i).signature)
                    && graft::Supernode::verifyHash(tx_hash, rta_hdr_src.keys.at(i), rta_signs_src.at(i).signature)) {

                rta_signs_dst[i] = rta_signs_src.at(i);
                MDEBUG("approval vote from: " << rta_hdr_src.keys.at(i));
            }
        }

        dst.extra2.clear(); // TODO - implement interface like cryptonote::put_graft_rta_signatures_to_extra2() which is supposed to overwrite existing signatures

        if (!cryptonote::add_graft_rta_signatures_to_extra2(dst.extra2, rta_signs_dst)) {
            MERROR("add_graft_rta_signatures_to_extra2 failed");
            return false;
        }

        return true;
    }


}

namespace graft::supernode::request {


// /core/autorize_rta_tx_request handler states
enum class AuthorizeRtaTxRequestHandlerState : int {
    IncomingRequest = 0,    // incoming request from client, store it and return Status::Again
    RequestStored,          // request stored, waiting to be processed: check if already processed
    PendingStatusUpdateSent,       // "payment status changed to pending" broadcast sent
    FailedStatusUpdateSent,       // "payment status changed to failed" broadcast sent
    KeyImagesRequestSent,   // "is_key_image_spent" request sent to cryptonode;
    KeyImagesSpentBroadcastSent, // check key images failed, Failed broadcast sent
    TxResposeSent           // "authorize_rta_tx_response" mullticast sent
};


// /core/autorize_rta_tx_response handler states
enum class AuthorizeRtaAuthResponseHandlerState : int {
    // Multicast call from cryptonode auth rta auth response
    IncomingRequest = 0,    // incoming request from cryptonode, store it for further processing and return Status::Ok
    RequestStored,          // request stored, waiting to be processed: check if already processed
    TransactionSentToPool,  // we have a CoA, pushed tx to tx pool, next is to broadcast status,
    StatusBroadcastSent     // we sent broadcast status, end of processing
};


enum class RtaTxState {
    Processing = 0,
    AlreadyProcessedApproved,
    AlreadyProcessedRejected
};


GRAFT_DEFINE_IO_STRUCT_INITED(IsKeyImageSpentRequest,
                              (std::vector<std::string>, key_images, std::vector<std::string>())
                              );

GRAFT_DEFINE_IO_STRUCT_INITED(IsKeyImageSpentResponse,
                              (std::vector<unsigned int>, spent_status, std::vector<unsigned int>()),
                              (std::string, status, std::string()),
                              (bool, untrusted, false)
                              );


std::vector<int> get_rta_key_indexes(const cryptonote::rta_header &rta_hdr, SupernodePtr supernode)
{
    std::vector<int> result;
    for (size_t i = 0; i < rta_hdr.keys.size(); ++i) {
        if (rta_hdr.keys.at(i) == supernode->idKey()) {
            result.push_back(i);
        }
    }
    return result;
}


bool removeInvalidSignatures(cryptonote::transaction &tx)
{

    cryptonote::rta_header rta_hdr;
    if (!cryptonote::get_graft_rta_header_from_extra(tx, rta_hdr)) {
        MERROR("failed get rta header from tx: " << cryptonote::get_transaction_hash(tx));
        return false;
    }
    std::vector<cryptonote::rta_signature> rta_signatures;
    if (!cryptonote::get_graft_rta_signatures_from_extra2(tx, rta_signatures)) {
        MERROR("failed to get rta signatures from tx");
        return false;
    }
    if (rta_hdr.keys.size() != rta_signatures.size()) {
        MERROR("number of keys and signatures mismatch, keys: " << rta_hdr.keys.size() << ", signatures: " << rta_signatures.size());
        return false;
    }
    crypto::hash tx_hash = cryptonote::get_transaction_hash(tx);

    std::vector<cryptonote::rta_signature> cleaned_signatures;
    for (size_t i  = 0; i < rta_hdr.keys.size(); ++i) {
        if (Supernode::verifyHash(tx_hash, rta_hdr.keys.at(i), rta_signatures.at(i).signature)) {
            cryptonote::rta_signature sig;
            sig.key_index = i;
            sig.signature = rta_signatures.at(i).signature;
            cleaned_signatures.push_back(sig);
        }
    }

    tx.extra2.clear();
    cryptonote::add_graft_rta_signatures_to_extra2(tx.extra2, cleaned_signatures);
    return true;
}


bool getQuorumState(const cryptonote::transaction &tx, size_t &auth_sample_votes, size_t &pos_and_proxy_votes)
{
    auth_sample_votes = pos_and_proxy_votes = 0;
    cryptonote::rta_header rta_hdr;
    if (!cryptonote::get_graft_rta_header_from_extra(tx, rta_hdr)) {
        MERROR("failed get rta header from tx: " << cryptonote::get_transaction_hash(tx));
        return false;
    }
    std::vector<cryptonote::rta_signature> rta_signatures;
    if (!cryptonote::get_graft_rta_signatures_from_extra2(tx, rta_signatures)) {
        MERROR("failed to get rta signatures from tx");
        return false;
    }
    if (rta_hdr.keys.size() != rta_signatures.size()) {
        MERROR("number of keys and signatures mismatch, keys: " << rta_hdr.keys.size() << ", signatures: " << rta_signatures.size());
        return false;
    }
    crypto::hash tx_hash = cryptonote::get_transaction_hash(tx);
    for (size_t i  = 0; i < rta_hdr.keys.size(); ++i) {
        if (Supernode::verifyHash(tx_hash, rta_hdr.keys.at(i), rta_signatures.at(i).signature)) {
            if (i < 3)
                ++pos_and_proxy_votes;
            else
                ++auth_sample_votes;
        }
    }
    return true;
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

#if 0 // negative "insufficient fee" test
    if (supernode->idKeyAsString() == "25b316d25e6c2dd8dd60fd983de9fbd5a9bb1fcf96d65bbb1c295708bafa00cb") {
        return false;
    }
#endif


    std::vector<std::pair<size_t, uint64_t>> outputs;
    uint64_t tx_amount = 0;


    cryptonote::address_parse_info address_pi;

    if (!cryptonote::get_account_address_from_str(address_pi, supernode->testnet() ? cryptonote::TESTNET : cryptonote::MAINNET, supernode->walletAddress())) {
        MERROR("Failed to parse account from: " << supernode->walletAddress());
        return false;
    }

    if (!Utils::get_tx_amount(address_pi.address, tx_key, tx, outputs, tx_amount)) {
        MERROR("Failed to get amount from tx");
        return false;
    }

    // TODO: check as percentage of actual amount
    return tx_amount > 0;
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
    std::vector<int> key_indexes = get_rta_key_indexes(rta_hdr, supernode);
    if (key_indexes.empty()) {
        MERROR("Internal error: tx doesn't contain our key " << cryptonote::get_transaction_hash(tx));
        return Status::Ok;
    }
    crypto::hash tx_hash = cryptonote::get_transaction_hash(tx);
    for (int idx : key_indexes) {
        cryptonote::rta_signature &rta_sign = rta_signatures.at(idx);
        supernode->signHash(tx_hash, rta_sign.signature);
    }

    cryptonote::add_graft_rta_signatures_to_extra2(tx.extra2, rta_signatures);

    if (!cryptonote::get_graft_rta_signatures_from_extra2(tx, rta_signatures)) {
        MERROR("Failed to get test signatures");
        abort();
    }

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
    MDEBUG("calling authorize_rta_tx_response for payment: " << rta_hdr.payment_id);
    return Status::Forward;

}


Status broadcastStatusUpdate(const RTAStatus status, const std::string &paymentId, SupernodePtr supernode, graft::Output &output)
{
    PaymentStatus req;
    req.PaymentID = paymentId;
    req.Status = static_cast<int>(status);
    if (!paymentStatusSign(supernode, req)) {
        MERROR("Failed to sign UpdateStatusBroadcast, payment id: " << req.PaymentID);
        return Status::Ok;
    }
    utils::buildBroadcastOutput(req, supernode, std::vector<std::string>(), "json_rpc/rta", "/core/update_payment_status", output);
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

    bool proxy_supernode = isProxySupernode(rta_hdr, supernode);
    bool auth_sample_supernode = isAuthSampleSupernode(rta_hdr, supernode);

    MDEBUG("processing rta tx for payment: " << rta_hdr.payment_id << ", proxy_supernode: " << proxy_supernode 
           << ", auth_sample_supernode: " << auth_sample_supernode);
    
    // sanity check if we're proxy supernode or auth sample supernode
    if (!proxy_supernode && !auth_sample_supernode) {
        MERROR("Internal error: authorize_rta_tx_request handled by non-proxy non-auth-sample supernode: " << rta_hdr.payment_id);
        return Status::Ok;
    }

    if (!checkMyFee(tx, tx_key, rta_hdr, supernode, ctx)) {
        MERROR("Failed to validate supernode fee for payment: " << rta_hdr.payment_id);
        ctx.local[RTA_TX_REQ_HANDLER_STATE_KEY] = AuthorizeRtaTxRequestHandlerState::FailedStatusUpdateSent;
        return broadcastStatusUpdate(RTAStatus::FailZeroFee, rta_hdr.payment_id, supernode, output);
    }

    // in case we're auth sample supernode - we should check for spent key images and own fee
    // in case we're proxy supernode - we should check only fee

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
    std::string payment_id = ctx.local["payment_id"];
    SupernodePtr supernode = ctx.global.get(CONTEXT_KEY_SUPERNODE, SupernodePtr());


    for (const auto spent_status : resp.spent_status) {
        if (spent_status != 0) {
            MERROR("some of the key images spent, status: " << spent_status);
            ctx.local[RTA_TX_REQ_HANDLER_STATE_KEY] = AuthorizeRtaTxRequestHandlerState::KeyImagesSpentBroadcastSent;
            return broadcastStatusUpdate(RTAStatus::FailDoubleSpend, payment_id, supernode, output);
        }
    }

#if 0 // test for "spent key-images" found
    if (supernode->idKeyAsString() == "25b316d25e6c2dd8dd60fd983de9fbd5a9bb1fcf96d65bbb1c295708bafa00cb") {
        ctx.local[RTA_TX_REQ_HANDLER_STATE_KEY] = AuthorizeRtaTxRequestHandlerState::KeyImagesSpentBroadcastSent;
        return broadcastStatusUpdate(RTAStatus::FailDoubleSpend, payment_id, supernode, output);
    }
#endif

    cryptonote::transaction tx;
    if (!getTxFromLocalContext(ctx, tx, CONTEXT_RTA_TX_REQ_TX_KEY)) {
        MERROR("Failed to read tx from context");
        return Status::Ok;
    }

    crypto::secret_key tx_key;

    if (!utils::getTxKeyFromContext(ctx, tx_key, payment_id + CONTEXT_RTA_TX_REQ_TX_KEY_KEY)) {
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

    MDEBUG("incoming tx auth response from: " << req.sender_address);
    ctx.local["request"] = tx_req;
    MDEBUG(__FUNCTION__ << " end");
    return sendAgainResponseToCryptonode(output);
}

#define DECRYPT_TX() \
   cryptonote::transaction tx; \
    if (!utils::decryptTxFromHex(pay_req.TxBlob, supernode, tx)) { \
        MERROR("Failed to decrypt tx from: " << pay_req.TxBlob); \
        return Status::Ok; \
   }

#define DECRYPT_TX_KEY() \
   \
   crypto::secret_key tx_key; \
   if (!utils::decryptTxKeyFromHex(pay_req.TxKey, supernode, tx_key)) { \
       MERROR("Failed to decrypt tx key from: " << pay_req.TxKey); \
       return Status::Ok; \
   }

#define GET_RTA_HDR() \
    \
    crypto::hash tx_hash = cryptonote::get_transaction_hash(tx); \
    cryptonote::rta_header rta_hdr; \
    if (!cryptonote::get_graft_rta_header_from_extra(tx, rta_hdr)) { \
        MERROR("Failed to read rta_header, non-rta tx: "<< tx_hash); \
        return Status::Ok; \
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

    DECRYPT_TX();
    DECRYPT_TX_KEY();
    GET_RTA_HDR();

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



        putTxToLocalContext(tx, ctx, CONTEXT_RTA_TX_REQ_TX_KEY); // TODO remove this duplicate tx, use only one from global context
        // store tx and tx key key
        utils::putTxToGlobalContext(tx, ctx, rta_hdr.payment_id + CONTEXT_RTA_TX_REQ_TX_KEY, SALE_TTL);
        utils::putTxKeyToContext(tx_key, ctx, rta_hdr.payment_id + CONTEXT_RTA_TX_REQ_TX_KEY_KEY, SALE_TTL);
        MDEBUG("tx stored in local context for payment: " << rta_hdr.payment_id);
        ctx.local[RTA_TX_REQ_HANDLER_STATE_KEY] = AuthorizeRtaTxRequestHandlerState::PendingStatusUpdateSent;
        ctx.local["payment_id"] = rta_hdr.payment_id;
        if (isAuthSampleSupernode(rta_hdr, supernode)) { // we can update status only if we're auth sample member
            return broadcastStatusUpdate(RTAStatus::InProgress, rta_hdr.payment_id, supernode, output);
        }
        // in case we're proxy node - we can't broadcast status update but still need the state machine working
        // (re-using the same state machine, so handler invoked same way as we've sent the status)
        return Status::Again;


    } else {
        MDEBUG("payment id auth request already processed: " << rta_hdr.payment_id);
    }

    return Status::Ok;

}


Status handleTxAuthRequestPendingStatusUpdated(const Router::vars_t& vars, const graft::Input& input,
                            graft::Context& ctx, graft::Output& output) noexcept
{
    BroadcastResponseFromCryptonodeJsonRpc resp;

    JsonRpcErrorResponse error;
    if (!input.get(resp) || resp.error.code != 0 || resp.result.status != STATUS_OK) {
        return  errorInternalError("Error multicasting payment status update", output);
    }

    cryptonote::transaction tx;
    if (!getTxFromLocalContext(ctx, tx, CONTEXT_RTA_TX_REQ_TX_KEY)) {
        MERROR("Failed to get tx from context");
        return Status::Ok;
    }
    std::string payment_id = ctx.local["payment_id"];

    crypto::secret_key tx_key;
    if (!utils::getTxKeyFromContext(ctx, tx_key, payment_id + CONTEXT_RTA_TX_REQ_TX_KEY_KEY)) {
        MERROR("Failed to get tx key from context");
        return Status::Ok;
    }

    return processNewRtaTx(tx, tx_key, ctx, output);
}

Status handleTxAuthRequestFailedStatusUpdated(const Router::vars_t& vars, const graft::Input& input,
                            graft::Context& ctx, graft::Output& output) noexcept
{
    return Status::Ok; // finite state
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

//    AuthorizeRtaTxRequestJsonRpcResponse out;
//    out.result.Result = STATUS_OK;
//    output.load(out);

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
Status handleRtaAuthResponse(const Router::vars_t& vars, const graft::Input& input,
                            graft::Context& ctx, graft::Output& output)
{

    try {

        MDEBUG(__FUNCTION__ << " begin");
        assert(ctx.local.getLastStatus() == Status::Again);

        if (!ctx.local.hasKey("request")) {
            LOG_ERROR("Internal error. no input for 'again' status");
            return Status::Error;
        }

        SupernodePtr supernode = ctx.global.get(CONTEXT_KEY_SUPERNODE, SupernodePtr());
        PayRequest pay_req = ctx.local["request"];

        DECRYPT_TX();
        GET_RTA_HDR();

        std::vector<cryptonote::rta_signature> rta_signs;
        if (!cryptonote::get_graft_rta_signatures_from_extra2(tx, rta_signs)) {
            MERROR("Failed to get rta_signatures for payment: " << rta_hdr.payment_id);
            MERROR("extra2 size: " << tx.extra2.size());
            abort();
        }

        ctx.local["payment_id"] = rta_hdr.payment_id;

        // check if payment already processed
        PaymentStatus paymentStatus = ctx.global.get(rta_hdr.payment_id + CONTEXT_KEY_STATUS, PaymentStatus());

        // RTAStatus status = static_cast<RTAStatus>(ctx.global.get(rta_hdr.payment_id + CONTEXT_KEY_STATUS, int(RTAStatus::None)));

        if (isFiniteRtaStatus(RTAStatus(paymentStatus.Status))) {
            MINFO("Payment: " << rta_hdr.payment_id << " is already in finite state: " << paymentStatus.Status);
            return Status::Ok;
        }

        MDEBUG("incoming rta tx response for payment: " << rta_hdr.payment_id);

        // check if payment id is known already (captured by "/core/store_payment_data" handler)
        if (!ctx.global.hasKey(rta_hdr.payment_id + CONTEXT_KEY_PAYMENT_DATA)) {
            MERROR("Payment id is unknown: " << rta_hdr.payment_id);
            return Status::Ok;
        }
        size_t auth_sample_votes, proxy_votes;
        auth_sample_votes = proxy_votes = 0;
        cryptonote::transaction local_tx;
        auto process_tx = [&tx, &local_tx, &rta_hdr, &auth_sample_votes, &proxy_votes, &ctx] () {
            CRITICAL_REGION_LOCAL1(vote_lock);
            // check if we have a stored tx
            if (!ctx.global.hasKey(rta_hdr.payment_id + CONTEXT_KEY_RTA_VOTING_TX)) {
                MDEBUG("voting tx not found, copying tx -> local_tx");
                local_tx = tx;

            } else {
                MDEBUG("voting tx found, merging with incoming tx");
                if (!utils::getTxFromGlobalContext(ctx, local_tx, rta_hdr.payment_id + CONTEXT_KEY_RTA_VOTING_TX)) {
                    MERROR("Internal error: failed to load tx from global context: " << rta_hdr.payment_id);
                    return false;
                }
                if (!mergeTxSignatures(tx, local_tx)) {
                    MERROR("Failed to merge tx signatures for payment: " << rta_hdr.payment_id);
                    return false;
                }
            }

            utils::putTxToGlobalContext(local_tx, ctx, rta_hdr.payment_id + CONTEXT_KEY_RTA_VOTING_TX, SALE_TTL);
            MDEBUG("voting tx stored in global context");
            MDEBUG("Dumping signatures: ");
            dumpSignatures(local_tx);
            if (!getQuorumState(local_tx, auth_sample_votes, proxy_votes)) {
                MERROR("Internal error: failied to get quorum state for payment: " << rta_hdr.payment_id);
                return false;
            }
            return true;
        };

        if (!process_tx()) {
            return Status::Ok;
        }


        MDEBUG("voting status: auth_sample_votes: " << auth_sample_votes << ", proxy_votes: " << proxy_votes);



        if (auth_sample_votes >= RTA_VOTES_TO_APPROVE && proxy_votes == 3) { // TODO: magic numbers to constants
            MDEBUG("CoA matches for payment: " << rta_hdr.payment_id
                   << " , pushing tx to pool");
            SendRawTxRequest req;

            if (!removeInvalidSignatures(local_tx)) {
                MERROR("Failed to clean up invalid signatures");
                return sendOkResponseToCryptonode(output); // stop processing
            }
            createSendRawTxRequest(local_tx, req);
            dumpSignatures(local_tx);

            output.load(req);
            output.path = "/sendrawtransaction";
            ctx.local[RTA_TX_RESP_HANDLER_STATE_KEY] = AuthorizeRtaAuthResponseHandlerState::TransactionSentToPool;
            // store tx in the local context for futher processing
            putTxToLocalContext(local_tx, ctx, CONTEXT_RTA_TX_REQ_TX_KEY);

            return Status::Forward;
        }
        return sendOkResponseToCryptonode(output); // no CoA yet, stop processing

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

    cryptonote::transaction tx;
    if (!getTxFromLocalContext(ctx, tx, CONTEXT_RTA_TX_REQ_TX_KEY)) {
        MERROR("Failed to read tx from context");
        return Status::Ok;
    }

    cryptonote::rta_header rta_hdr;
    if (!cryptonote::get_graft_rta_header_from_extra(tx, rta_hdr)) {
        MERROR("failed get rta header from tx");
        return Status::Ok;
    }

    MDEBUG("processing sendrawtransaction reply for payment: " << rta_hdr.payment_id);

    // cryptonote::get_transaction_hash(tx);

    // RTAStatus status = static_cast<RTAStatus>(ctx.global.get(rta_hdr.payment_id + CONTEXT_KEY_STATUS, int(RTAStatus::None)));
    PaymentStatus paymentStatus = ctx.global.get(rta_hdr.payment_id + CONTEXT_KEY_STATUS, PaymentStatus());

    if (RTAStatus(paymentStatus.Status) == RTAStatus::None) {
        LOG_ERROR("can't find status for payment_id: " << rta_hdr.payment_id);
        return Status::Ok;
    }

    SendRawTxResponse resp;

    if (!input.get(resp)) {
        LOG_ERROR("Failed to parse input: " << input.data());
        return Status::Ok;
    }

    if (isFiniteRtaStatus(RTAStatus(paymentStatus.Status))) {
        MWARNING("payment: " << rta_hdr.payment_id << ", most likely already processed,  status: " << paymentStatus.Status);
        return sendOkResponseToCryptonode(output);
    }

    if (resp.status != "OK") {
        // check for double spend
        if (resp.double_spend) { // specific cast, we can get cryptonode's /sendrawtransaction response before status broadcast,
                                 // just ignore it for now
            LOG_ERROR("double spend for payment: " << rta_hdr.payment_id << ", tx: " << cryptonote::get_transaction_hash(tx));
            return sendOkResponseToCryptonode(output);
        }
        paymentStatus.Status = static_cast<int>(RTAStatus::FailTxRejected);
        // LOG_ERROR("failed to put tx to pool: " << tx_id << ", reason: " << resp.reason);
        LOG_ERROR("failed to put tx to pool: " << cryptonote::get_transaction_hash(tx) << ", input: " << input.data());
    } else {
        paymentStatus.Status = static_cast<int>(RTAStatus::Success);
    }

    SupernodePtr supernode = ctx.global.get(CONTEXT_KEY_SUPERNODE, SupernodePtr());

    MDEBUG("broadcasting status for payment id: " << rta_hdr.payment_id << ", status : " << paymentStatus.Status);

    if (!paymentStatusSign(supernode, paymentStatus)) {
        MERROR("Failed to sign update payment status: " << paymentStatus.PaymentID);
        return Status::Ok;
    }

    utils::buildBroadcastOutput(paymentStatus, supernode, std::vector<std::string>(), "json_rpc/rta", "/core/update_payment_status", output);

    ctx.local[RTA_TX_RESP_HANDLER_STATE_KEY] = AuthorizeRtaAuthResponseHandlerState::StatusBroadcastSent;
    MDEBUG(__FUNCTION__ << " end");
    return Status::Forward;
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
        case AuthorizeRtaTxRequestHandlerState::PendingStatusUpdateSent: // pending status update broadcast sent;
            return handleTxAuthRequestPendingStatusUpdated(vars, input, ctx, output);
        case AuthorizeRtaTxRequestHandlerState::FailedStatusUpdateSent: // failed status update broadcast sent;
            return handleTxAuthRequestFailedStatusUpdated(vars, input, ctx, output);
        case AuthorizeRtaTxRequestHandlerState::KeyImagesRequestSent: // is_key_images_spent request sent;
            return processKeyImagesReply(vars, input, ctx, output);
        case AuthorizeRtaTxRequestHandlerState::KeyImagesSpentBroadcastSent: // some of the key images found spent
            return Status::Ok; // we got cryptonode reply, stop processing;
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
 * \brief handleAuthorizeRtaTxResponse - handles supernode's RTA auth response /core/authorize_rta_tx_response
 * \param vars
 * \param inputRtaAuthResponseHandlerState
 * \param ctx
 * \param output
 * \return
 */
Status handleAuthorizeRtaTxResponse(const Router::vars_t& vars, const graft::Input& input,
                                 graft::Context& ctx, graft::Output& output) noexcept
{


    try {
        AuthorizeRtaAuthResponseHandlerState state = ctx.local.hasKey(RTA_TX_RESP_HANDLER_STATE_KEY) ? ctx.local[RTA_TX_RESP_HANDLER_STATE_KEY]
                                                                                                   : AuthorizeRtaAuthResponseHandlerState::IncomingRequest;

        MDEBUG(__FUNCTION__ << " state: " << int(state) << ", status: "<< (int) ctx.local.getLastStatus() << ", task id: " << boost::uuids::to_string(ctx.getId()));

        switch (state) {
        // actually not a reply, just incoming multicast. same as "called by client" and client is cryptonode here
        case AuthorizeRtaAuthResponseHandlerState::IncomingRequest:
            ctx.local[RTA_TX_RESP_HANDLER_STATE_KEY] = AuthorizeRtaAuthResponseHandlerState::RequestStored;
            return storeRtaTxAndReplyOk(vars, input, ctx, output);

        case AuthorizeRtaAuthResponseHandlerState::RequestStored:
            return handleRtaAuthResponse(vars, input, ctx, output);

        case AuthorizeRtaAuthResponseHandlerState::TransactionSentToPool:
            return handleCryptonodeTxPushResponse(vars, input, ctx, output);

        case AuthorizeRtaAuthResponseHandlerState::StatusBroadcastSent:
            return handleCryptonodeReply(vars, input, ctx, output);


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

