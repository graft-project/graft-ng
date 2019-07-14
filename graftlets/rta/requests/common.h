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

#include <chrono>


namespace graft::supernode::request {

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

/*!
 * \brief encryptTxToHex - encrypts transaction using public keys (one-to-many scheme)
 * \param tx             - transaction to encrypt
 * \param keys           - keys
 * \param encryptedHex   - output encoded as hexadecimal string
 */
void encryptTxToHex(const cryptonote::transaction &tx, const std::vector<crypto::public_key> &keys, std::string &encryptedHex);


bool decryptTxKeyFromHex(const std::string &encryptedHex, SupernodePtr supernode, crypto::secret_key &tx_key);
void encryptTxKeyToHex(const crypto::secret_key &tx_key, const std::vector<crypto::public_key> &keys, std::string &encryptedHex);


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


} // namespace utils

} // namepace graft::supernode::request

