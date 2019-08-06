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

#include "storepaymentdata.h"
#include "common.h"
#include "sale.h"

#include "supernode/requests/sale_status.h"

#include "supernode/requestdefines.h"
#include "lib/graft/requesttools.h"
#include "rta/supernode.h"
#include "rta/fullsupernodelist.h"
#include "supernode/requests/broadcast.h"
#include "utils/cryptmsg.h" // one-to-many message cryptography


#undef MONERO_DEFAULT_LOG_CATEGORY
#define MONERO_DEFAULT_LOG_CATEGORY "supernode.storepaymentdatarequest"

namespace graft::supernode::request {
/*!
 * \brief storePaymentDataRequest - handles /core/store_payment_data - call coming from cryptonode (multicasted), we just need to store "payment_data"
 * \param vars
 * \param input
 * \param ctx
 * \param output
 * \return
 */
Status storePaymentDataRequest(const Router::vars_t& vars, const graft::Input& input,
                             graft::Context& ctx, graft::Output& output)
{
    BroadcastRequestJsonRpc reqjrpc;
    if (!input.get(reqjrpc)) {
        return errorInvalidParams(output);
    }

    // JSON-RPC envelop, we need to extract actual request;

    BroadcastRequest &req = reqjrpc.params;

    if (!utils::verifyBroadcastMessage(req, req.sender_address)) {
        return errorInvalidSignature(output);
    }


    graft::Input innerInput;
    innerInput.load(req.data);
    SaleRequest saleRequest;  // re-using sale container

    if (!innerInput.getT<serializer::JSON_B64>(saleRequest)) {
        return errorInvalidParams(output);
    }

    const std::string payment_id = saleRequest.PaymentID;
    MDEBUG("payment data received from multicast for payment id: " << payment_id);

    SupernodePtr supernode = ctx.global.get(CONTEXT_KEY_SUPERNODE, SupernodePtr(nullptr));

    // check if it addressed to me; decrypt; store if both successful
    do {
        if (req.receiver_addresses.empty()
                || std::find(req.receiver_addresses.begin(),
                             req.receiver_addresses.end(), supernode->idKeyAsString()) != req.receiver_addresses.end()) {

            std::string decryptedPaymentBlob, encryptedPaymentBlob;

            if (!epee::string_tools::parse_hexstr_to_binbuff(saleRequest.paymentData.EncryptedPayment, encryptedPaymentBlob)) {
                MERROR("Failed to parse encrypted payment for payment id: " << payment_id);
                break;
            }

            if (!graft::crypto_tools::decryptMessage(encryptedPaymentBlob, supernode->secretKey(), decryptedPaymentBlob)) {
                MERROR("Failed to decrypt payment data for payment id: " << payment_id);
                break;
            }

            MDEBUG("decrypted payment : " << decryptedPaymentBlob);

            if (!ctx.global.hasKey(payment_id + CONTEXT_KEY_PAYMENT_DATA)) {
                // TODO: clenup after payment done;
                GlobalContextMap::OnExpired on_expired = [payment_id, &ctx](std::pair<std::string, std::any> &arg) {
                    MDEBUG("on_expired: payment_id expired: " << payment_id);
                    MDEBUG("payment status available: " << ctx.global.hasKey(payment_id + CONTEXT_KEY_STATUS));
                };
                MDEBUG("storing payment data in global context: " << payment_id << ", timeout: " << SALE_TTL.count());
                ctx.global.set(payment_id + CONTEXT_KEY_PAYMENT_DATA, saleRequest.paymentData, SALE_TTL, on_expired);
            } else {
                MWARNING("payment " << payment_id << " already known");
            }
        }
    } while (false);

    BroadcastResponseToCryptonodeJsonRpc resp;
    resp.result.status = "OK";
    output.load(resp);

    return Status::Ok;
}


}

