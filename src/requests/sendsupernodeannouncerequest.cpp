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

#include "sendsupernodeannouncerequest.h"
#include "requestdefines.h"
#include "sendrawtxrequest.h"
#include "rta/fullsupernodelist.h"
#include "rta/supernode.h"

#include <misc_log_ex.h>
#include <boost/shared_ptr.hpp>


#undef MONERO_DEFAULT_LOG_CATEGORY
#define MONERO_DEFAULT_LOG_CATEGORY "supernode.sendsupernodeannouncerequest"

namespace {
    static const char * PATH = "/send_supernode_announce";

}


namespace graft {

/**
 * @brief
 * @param vars
 * @param input
 * @param ctx
 * @param output
 * @return
 */
Status handleSupernodeAnnounce(const Router::vars_t& vars, const graft::Input& input,
                                 graft::Context& ctx, graft::Output& output)
{
    LOG_PRINT_L1(PATH << " called with payload: " << input.data());
    // TODO: implement DOS protection, ignore too frequent requests

    boost::shared_ptr<FullSupernodeList> fsl = ctx.global.get("fsl", boost::shared_ptr<FullSupernodeList>());
    SupernodePtr supernode = ctx.global.get("supernode", SupernodePtr());


    if (!fsl.get()) {
        LOG_ERROR("Internal error. Supernode list object missing");
        return Status::Error;
    }

    if (!supernode.get()) {
       LOG_ERROR("Internal error. Supernode object missing");
       return Status::Error;
    }

    SendSupernodeAnnounceJsonRpcRequest req;

    if (!input.get(req) ) { // can't parse request
        LOG_ERROR("Failed to parse request");
        return Status::Error;
    }

    //  handle announce
    const SupernodeAnnounce & announce = req.params;
    MINFO("received announce for address: " << announce.address);

    if (fsl->exists(announce.address)) {
        if (!fsl->get(announce.address)->updateFromAnnounce(announce)) {
            LOG_ERROR("Failed to update supernode with announce");
            return Status::Error;
        }
    } else {
        std::string watchonly_wallets_path = ctx.global["watchonly_wallets_path"];
        assert(!watchonly_wallets_path.empty());
        boost::filesystem::path p(watchonly_wallets_path);
        p /= announce.address;
        std::string wallet_path = p.string();
        std::string cryptonode_rpc_address = ctx.global["cryptonode_rpc_address"];
        bool testnet = ctx.global["testnet"];
        MINFO("creating wallet in: " << p.string());

        Supernode * s  = Supernode::createFromAnnounce(wallet_path, announce,
                                                       cryptonode_rpc_address,
                                                       testnet);
        if (!s) {
            LOG_ERROR("Cant create watch-only supernode wallet for address: " << announce.address);
            return Status::Error;

        }

        MINFO("About to add supernode to list [" << s << "]: " << s->walletAddress());
        if (!fsl->add(s)) {
            // DO NOT delete "s" here, it will be deleted by smart pointer;
            LOG_ERROR("Can't add new supernode to list [" << s << "]" << s->walletAddress());
        }
    }
    return Status::Ok;

}

Status sendSupernodeAnnounceHandler(const Router::vars_t& vars, const graft::Input& input,
                                graft::Context& ctx, graft::Output& output)
{

    enum class State : int {
        IncomingRequest = 0,
        HandleAnnounce
    };

    State state = ctx.local.hasKey(__FUNCTION__) ? ctx.local[__FUNCTION__] : State::IncomingRequest;
    MINFO("begin, state: " << int(state));
    switch (state) {
    case State::IncomingRequest:
        ctx.local[__FUNCTION__] = State::HandleAnnounce;
        return storeRequestAndReplyOk<SendSupernodeAnnounceJsonRpcResponse>(vars, input, ctx, output);
    case State::HandleAnnounce:
        return handleSupernodeAnnounce(vars, input, ctx, output);
    default:
        LOG_ERROR("Unhandled state: " << (int)state);
        abort();
    }

}


void registerSendSupernodeAnnounceRequest(graft::Router &router)
{
    Router::Handler3 h3(nullptr, sendSupernodeAnnounceHandler, nullptr);

    router.addRoute(PATH, METHOD_POST, h3);
    LOG_PRINT_L0("route " << PATH << " registered");
}


}
