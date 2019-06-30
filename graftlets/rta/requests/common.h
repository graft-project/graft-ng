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

GRAFT_DEFINE_IO_STRUCT(EncryptedNodeKey,
                       (std::string, Id),
                       (std::string, Key)
                       );

// to be encrypted and used as PaymentData::EncryptedPayment param
GRAFT_DEFINE_IO_STRUCT_INITED(PaymentInfo,
                        (std::string, Details, std::string()),
                        (uint64_t, Amount, 0)
                        );


// TODO: move to someting like "requests.h"
GRAFT_DEFINE_IO_STRUCT_INITED(PaymentData,
                              (std::string, EncryptedPayment, std::string()), // encrypted payment data (incl amount)
                              (std::vector<EncryptedNodeKey>, AuthSampleKeys, std::vector<EncryptedNodeKey>()),  // encrypted message keys (one-to-many encryption)
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

}

}

