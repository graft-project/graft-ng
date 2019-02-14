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

#include "supernode/requests/send_raw_tx.h"
#include "supernode/requestdefines.h"
#include <misc_log_ex.h>

#undef MONERO_DEFAULT_LOG_CATEGORY
#define MONERO_DEFAULT_LOG_CATEGORY "supernode.sendrawtxrequest"

namespace graft::supernode::request {

Status sendRawTxHandler(const Router::vars_t& vars, const graft::Input& input,
                                 graft::Context& ctx, graft::Output& output)
{
    // call from client
    if (!ctx.local.hasKey(__FUNCTION__)) {
        LOG_PRINT_L2("call from client, forwarding to cryptonode...");

        // just forward input to cryptonode
        SendRawTxRequest req = input.get<SendRawTxRequest>();
        output.load(req);
        ctx.local[__FUNCTION__] = true;
        return Status::Forward;
    } else {
    // response from cryptonode
        LOG_PRINT_L2("response from cryptonode : " << input.data());
        SendRawTxResponse resp = input.get<SendRawTxResponse>();
        if (resp.status == "OK") { // positive reply
            output.load(resp);
            return Status::Ok;
        } else {
            ErrorResponse ret;
            ret.code = ERROR_INTERNAL_ERROR;
            ret.message = resp.reason;
            output.load(ret);
            return Status::Error;
        }
    }
}

void registerSendRawTxRequest(graft::Router& router)
{
    Router::Handler3 h3(nullptr, sendRawTxHandler, nullptr);
    const char * path = "/cryptonode/sendrawtx";
    router.addRoute(path, METHOD_POST, h3);
    LOG_PRINT_L2("route " << path << " registered");
}

bool createSendRawTxRequest(const tools::wallet2::pending_tx& ptx, SendRawTxRequest& request)
{
    assert(ptx.dests.size() == 1);

//    for (const auto &dest : ptx.dests) {
//        request.tx_info.amount += dest.amount;
//    }

//    request.tx_info.fee = ptx.fee;
//    request.tx_info.dest_address = cryptonote::get_account_address_as_str(true, ptx.dests[0].addr);
//    request.tx_info.id = epee::string_tools::pod_to_hex(cryptonote::get_transaction_hash(ptx.tx));
    request.tx_as_hex = epee::string_tools::buff_to_hex_nodelimer(cryptonote::tx_to_blob(ptx.tx));

    return true;
}

bool createSendRawTxRequest(const cryptonote::transaction& tx, SendRawTxRequest& request)
{
    request.tx_as_hex = epee::string_tools::buff_to_hex_nodelimer(cryptonote::tx_to_blob(tx));
    return true;
}

}

