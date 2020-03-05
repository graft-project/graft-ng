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

#include "getpaymentdata.h"
#include "supernode/requests/common.h"
#include "paymentdatarequest.h"
#include "sale.h"

#include "supernode/requests/sale_status.h"

#include "supernode/requestdefines.h"
#include "lib/graft/requesttools.h"
#include "rta/supernode.h"
#include "rta/fullsupernodelist.h"
#include "lib/graft/common/utils.h"
#include "supernode/requests/broadcast.h"
#include "utils/cryptmsg.h" // one-to-many message cryptography


#undef MONERO_DEFAULT_LOG_CATEGORY
#define MONERO_DEFAULT_LOG_CATEGORY "supernode.getpaymentdatarequest"

namespace graft::supernode::request::getpaymentdata {

Status handleClientPaymentDataRequest(const Router::vars_t& vars, const graft::Input& input,
                             graft::Context& ctx, graft::Output& output)
{

    PaymentDataRequest req;
    if (!input.get(req)) {
        return errorInvalidParams(output);
    }
    FullSupernodeListPtr fsl = ctx.global.get(CONTEXT_KEY_FULLSUPERNODELIST, FullSupernodeListPtr(nullptr));
    SupernodePtr supernode = ctx.global.get(CONTEXT_KEY_SUPERNODE, SupernodePtr());


    // we have payment data
    if (ctx.global.hasKey(req.PaymentID + CONTEXT_KEY_PAYMENT_DATA)) {
        MDEBUG("payment data found for payment id: " << req.PaymentID);
        PaymentData paymentData = ctx.global.get(req.PaymentID + CONTEXT_KEY_PAYMENT_DATA, PaymentData());


        std::vector<std::string> auth_sample;
        for (const auto &item : paymentData.AuthSampleKeys) {
            auth_sample.push_back(item.Id);
        }

        if (!fsl->checkAuthSample(req.BlockHeight, req.BlockHash, req.PaymentID, auth_sample)) {
            std::string msg = "Failed to check auth sample for payment :" + req.PaymentID;
            return errorCustomError(msg, ERROR_RTA_FAILED, output);
        }
        PaymentDataResponse resp;
        resp.paymentData = paymentData;
        resp.WalletProxy.Id = supernode->idKeyAsString();
        resp.WalletProxy.WalletAddress = supernode->walletAddress();
        output.load(resp);
        return Status::Ok;
    } else {
        // we don't have a payment data for given payment id, request it from one of the randomly selected supernode from auth sample;
        // do not process the same request multiple times
        bool pending_request = ctx.global.get(req.PaymentID + CONTEXT_KEY_PAYMENT_DATA_PENDING, false);
        if (pending_request) {
            output.reset();
            output.resp_code = 202;
            return Status::Ok;
        }
        ctx.global.set(req.PaymentID + CONTEXT_KEY_PAYMENT_DATA_PENDING, true, SALE_TTL);

        MDEBUG("payment data NOT found for payment id: " << req.PaymentID);
        FullSupernodeList::supernode_array auth_sample;
        uint64_t block_num = 0;
        if (!fsl->buildAuthSample(req.BlockHeight, req.PaymentID, auth_sample, block_num)) {
            std::string msg = "Failed to build auth sample for payment id: " + req.PaymentID;
            MERROR(msg);
            return errorCustomError(msg, ERROR_RTA_FAILED, output);
        }

        // select random supernode
        // TODO: extract this as a template method
        PaymentDataRemoteRequest innerReq;
        innerReq.PaymentID = req.PaymentID;
        Output innerOut;
        innerOut.loadT<serializer::JSON_B64>(innerReq);
        BroadcastRequestJsonRpc out;
        out.method = "broadcast";
        out.params.callback_uri = "/core/payment_data_request";
        out.params.data = innerOut.data();
        out.params.sender_address = supernode->idKeyAsString();
        size_t maxIndex = auth_sample.size() - 1;
        size_t randomIndex = graft::utils::random_number<size_t>(0, maxIndex);

        out.params.receiver_addresses.push_back(auth_sample.at(randomIndex)->idKeyAsString());

        MDEBUG("requesting from remote peer: " << out.params.receiver_addresses.at(0));
        utils::signBroadcastMessage(out.params, supernode);
        output.load(out);
        output.path = "/json_rpc/rta";
        MDEBUG("unicasting: " << output.data());
        return Status::Forward;
    }
}

Status handleCryptonodeReply(const Router::vars_t& vars, const graft::Input& input,
                                graft::Context& ctx, graft::Output& output)
{
    // check cryptonode reply
    MDEBUG(__FUNCTION__ << " begin");
    BroadcastResponseFromCryptonodeJsonRpc resp;

    JsonRpcErrorResponse error;
    if (!input.get(resp) || resp.error.code != 0 || resp.result.status != STATUS_OK) {
        return errorCustomError("Error unicasting request", ERROR_INTERNAL_ERROR, output);
    }
    output.reset();
    output.resp_code = 202;
    return Status::Ok;
}

} // namespace graft::supernode::request::getpaymentdata


namespace graft::supernode::request {

Status getPaymentDataRequest(const Router::vars_t &vars, const Input &input, Context &ctx, Output &output)
{
    enum class GetPaymentDataHandlerState : int
    {
        ClientRequest = 0, // incoming wallet request
        CryptonodeReply    // cryptonode forward reply
    };


    GetPaymentDataHandlerState state = ctx.local.hasKey(__FUNCTION__) ? ctx.local[__FUNCTION__] : GetPaymentDataHandlerState::ClientRequest;
    // state machine to perform a) multicast to cryptonode
    switch (state) {
    // client requested "/get_payment_data"
    case GetPaymentDataHandlerState::ClientRequest:
        ctx.local[__FUNCTION__] = GetPaymentDataHandlerState::CryptonodeReply;
        return getpaymentdata::handleClientPaymentDataRequest(vars, input, ctx, output);
    case GetPaymentDataHandlerState::CryptonodeReply:
        // handle "unicast" response from cryptonode, check it's status;
        MDEBUG("GetPaymentData unicast response from cryptonode: " << input.data());
        MDEBUG("status: " << (int)ctx.local.getLastStatus());
        return getpaymentdata::handleCryptonodeReply(vars, input, ctx, output);
    default:
        LOG_ERROR("Internal error: unhandled state");
        abort();
    };
}

}


