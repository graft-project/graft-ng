// Copyright (c) 2019, The Graft Project
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


#pragma once

#include "lib/graft/serialize.h"
#include "rta/supernode.h"
#include "supernode/requests/broadcast.h"
#include <cryptonote_basic/cryptonote_basic.h>
#include <cryptonote_basic/cryptonote_format_utils.h>
#include <chrono>


namespace graft::supernode::request {

static const std::string RTA_TX_REQ_HANDLER_STATE_KEY = "rta_tx_req_handler_state";
static const std::string CONTEXT_RTA_TX_REQ_TX_KEY = "rta_tx_req_tx_key"; // key to store transaction
static const std::string CONTEXT_RTA_TX_REQ_TX_KEY_KEY = "rta_tx_req_tx_key_key"; // key to store transaction private key

static const std::string RTA_TX_RESP_HANDLER_STATE_KEY = "rta_tx_resp_handler_state";
static const std::string CONTEXT_RTA_TX_RESP_TX_KEY = "rta_tx_resp_tx_key"; // key to store transaction
static const std::string CONTEXT_RTA_TX_RESP_TX_KEY_KEY = "rta_tx_resp_tx_key_key"; // key to store transaction private key

// shared constants
extern const std::chrono::seconds SALE_TTL;


GRAFT_DEFINE_IO_STRUCT(NodeAddress,
                       (std::string, Id),
                       (std::string, WalletAddress)
                       );

GRAFT_DEFINE_IO_STRUCT(NodeId,
                      (std::string, Id)
                      );

// to be encrypted and used as PaymentData::EncryptedPayment param
GRAFT_DEFINE_IO_STRUCT_INITED(PaymentInfo,
                        (std::string, Details, std::string()),
                        (uint64_t, Amount, 0)
                        );


// TODO: move to someting like "requests.h"
GRAFT_DEFINE_IO_STRUCT_INITED(PaymentData,
                              (std::string, EncryptedPayment, std::string()),                  // serialized and encrypted PaymentInfo (hexadecimal)
                              (std::vector<NodeId>, AuthSampleKeys, std::vector<NodeId>()),
                              (NodeAddress, PosProxy, NodeAddress())
                              );


GRAFT_DEFINE_IO_STRUCT_INITED(PaymentStatus, // TODO: duplicate: same struct defined in 'getpaymentstatus.h'
    (std::string, PaymentID, std::string()),
    (uint64_t, PaymentBlock, 0),
    (int, Status, 0),
    (std::string, Signature, std::string()) // signature's hash is contatenated paymentid + to_string(Status)
);


GRAFT_DEFINE_IO_STRUCT(EncryptedPaymentStatus,
(std::string, PaymentID),
(std::string, PaymentStatusBlob)  // encrypted PaymentStatus
);


crypto::hash paymentStatusGetHash(const PaymentStatus &req);
bool paymentStatusSign(graft::SupernodePtr supernode, PaymentStatus &req);
bool paymentStatusSign(const crypto::public_key &pkey, const crypto::secret_key &skey, PaymentStatus &req);
bool paymentStatusEncrypt(const PaymentStatus &in, const std::vector<crypto::public_key> &keys,  EncryptedPaymentStatus &out);
bool paymentStatusDecrypt(const EncryptedPaymentStatus &in, const crypto::secret_key &key, PaymentStatus &out);

namespace utils {

/*!
 * \brief getRequestHash - builds hash for given broadcast request
 * \param req            - in request
 * \param hash           - out hash
 */
void getRequestHash(const BroadcastRequest &req, crypto::hash &hash);

/*!
 * \brief signBroadcastMessage - signs broadcast request
 * \param request              - in request
 * \param supernode            - signer supernode
 * \return                     - true on success
 */
bool signBroadcastMessage(BroadcastRequest &request, const SupernodePtr &supernode);

/*!
 * \brief verifyBroadcastMessage - verifies broadcast request signature
 * \param request                - in request
 * \param publicId               - public key in hexadecimal
 * \return                       - true on success
 */
bool verifyBroadcastMessage(BroadcastRequest &request, const std::string &publicId);

/*!
 * \brief decryptTx     - decrypts transaction from encrypted hexadecimal string
 * \param encryptedHex  - input data
 * \param supernode     - recipient supernode to decrypt with
 * \param tx            - output tx
 * \return              - true on success
 */
bool decryptTxFromHex(const std::string &encryptedHex, SupernodePtr supernode, cryptonote::transaction &tx);
bool decryptTxKeyFromHex(const std::string &encryptedHex, SupernodePtr supernode, crypto::secret_key &tx_key);



template <typename Request>
void buildBroadcastOutput(const Request &req, SupernodePtr & supernode, const std::vector<std::string> &destinations,
                          const std::string &path, const std::string &callback_uri, Output &output)
{
    Output innerOut;
    innerOut.loadT<serializer::JSON_B64>(req);
    BroadcastRequest bcast;
    bcast.callback_uri = callback_uri;
    bcast.sender_address = supernode->idKeyAsString();
    bcast.data = innerOut.data();
    bcast.receiver_addresses = destinations;

    utils::signBroadcastMessage(bcast, supernode);

    BroadcastRequestJsonRpc bcast_jsonrpc;
    bcast_jsonrpc.method = "broadcast";
    bcast_jsonrpc.params = std::move(bcast);

    output.load(bcast_jsonrpc);
    output.path = path;

}


void putTxToGlobalContext(const cryptonote::transaction &tx,  graft::Context &ctx, const std::string &key, std::chrono::seconds ttl);

bool getTxFromGlobalContext(graft::Context &ctx, cryptonote::transaction &tx, const std::string &key);

void putTxKeyToContext(const crypto::secret_key &txkey, graft::Context &ctx, const std::string &key, std::chrono::seconds ttl);

bool getTxKeyFromContext(graft::Context &ctx, crypto::secret_key &key, const std::string &payment_id);






} // namespace utils

} // namepace graft::supernode::request

