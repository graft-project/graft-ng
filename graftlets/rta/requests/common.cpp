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

#include "common.h"

#include <string_tools.h> // graftnoded's contrib/epee/include
#include <misc_log_ex.h>  // graftnoded's log macros

#include <cryptonote_basic/cryptonote_basic.h>
#include <cryptonote_basic/cryptonote_format_utils.h>
#include <utils/cryptmsg.h> // one-to-many message cryptography
#include <boost/algorithm/string/join.hpp>


namespace graft::supernode::request::utils {

void getRequestHash(const BroadcastRequest &req, crypto::hash &hash)
{
    // TODO: forward slashes in callback_uri are escaped as \/ so signature wont match while processing it;
    // either a) escape on the sender side or b) unescape on receiver side or c) exclude it from hash
    // (quick-and-dirty, picking "c" for now)

    std::string msg = /*boost::algorithm::join(req.receiver_addresses, "") +*/ req.sender_address /*+ req.callback_uri*/
            + req.data;
    hash = crypto::cn_fast_hash(msg.data(), msg.length());
}


bool signBroadcastMessage(BroadcastRequest &request, const SupernodePtr &supernode)
{

    crypto::hash hash;
    getRequestHash(request, hash);

    MDEBUG("Signing hash: " << hash << " with id: " << request.sender_address << "(" << supernode->idKeyAsString() << ")" );
    crypto::signature sign;
    bool result = supernode->signHash(hash, sign);
    request.signature = epee::string_tools::pod_to_hex(sign);

    return result;
}


bool verifyBroadcastMessage(BroadcastRequest &request, const std::string &publicId)
{
    crypto::hash hash;
    getRequestHash(request, hash);

    MDEBUG("Verifying hash: " << hash << " with id: " << request.sender_address << "(" << publicId << ")" );
    crypto::signature sign;
    if (!epee::string_tools::hex_to_pod(request.signature, sign)) {
        LOG_ERROR("Failed to deserialize signature from: " << request.signature);
        return false;
    }
    crypto::public_key pkey;
    if (!epee::string_tools::hex_to_pod(publicId, pkey)) {
        LOG_ERROR("Failed to deserialize public key from: " << publicId);
        return false;
    }
    return Supernode::verifyHash(hash, pkey, sign);
}

bool decryptTxFromHex(const std::string &encryptedHex, SupernodePtr supernode, cryptonote::transaction &tx)
{
    std::string decryptedTxBlob, encryptedTxBlob;

    if (!epee::string_tools::parse_hexstr_to_binbuff(encryptedHex, encryptedTxBlob)) {
        MERROR("failed to deserialize encrypted tx blob");
        return false;
    }

    if (!graft::crypto_tools::decryptMessage(encryptedTxBlob, supernode->secretKey(), decryptedTxBlob)) {
        MERROR("Failed to decrypt tx");
        return false;
    }

    if (!cryptonote::parse_and_validate_tx_from_blob(decryptedTxBlob, tx)) {
        MERROR("Failed to parse transaction from blob");
        return false;
    }
    return true;
}

void encryptTxToHex(const cryptonote::transaction &tx, const std::vector<crypto::public_key> &keys, std::string &encryptedHex)
{
    std::string buf;
    graft::crypto_tools::encryptMessage(cryptonote::tx_to_blob(tx), keys, buf);
    encryptedHex = epee::string_tools::buff_to_hex_nodelimer(buf);

}

bool decryptTxKeyFromHex(const std::string &encryptedHex, SupernodePtr supernode, crypto::secret_key &tx_key)
{
    std::string decryptedBlob, encryptedBlob;
    if (!epee::string_tools::parse_hexstr_to_binbuff(encryptedHex, encryptedBlob)) {
        MERROR("failed to deserialize encrypted tx key blob");
        return false;
    }

    if (!graft::crypto_tools::decryptMessage(encryptedBlob, supernode->secretKey(), decryptedBlob)) {
        MERROR("Failed to decrypt tx");
        return false;
    }

    memcpy(&tx_key, decryptedBlob.c_str(), decryptedBlob.size());
    return true;
}


void encryptTxKeyToHex(const crypto::secret_key &tx_key, const std::vector<crypto::public_key> &keys, std::string &encryptedHex)
{
    std::string buf;
    std::string tx_buf(reinterpret_cast<const char*>(&tx_key), sizeof(crypto::secret_key));
    graft::crypto_tools::encryptMessage(tx_buf, keys, buf);
    encryptedHex = epee::string_tools::buff_to_hex_nodelimer(buf);
}




} // namespace graft::supernode::request
