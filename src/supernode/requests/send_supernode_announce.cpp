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

#include "supernode/requests/send_supernode_announce.h"
#include "supernode/requests/send_raw_tx.h"
#include "supernode/requestdefines.h"
#include "rta/fullsupernodelist.h"
#include "rta/supernode.h"

#include <misc_log_ex.h>
#include <boost/shared_ptr.hpp>


#undef MONERO_DEFAULT_LOG_CATEGORY
#define MONERO_DEFAULT_LOG_CATEGORY "supernode.sendsupernodeannouncerequest"

namespace {
    static const char* PATH = "/send_supernode_announce";

}

namespace graft::supernode::request {

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

    boost::shared_ptr<FullSupernodeList> fsl = ctx.global.get(CONTEXT_KEY_FULLSUPERNODELIST,
                                                              boost::shared_ptr<FullSupernodeList>());
    SupernodePtr supernode = ctx.global.get(CONTEXT_KEY_SUPERNODE, SupernodePtr());


    if (!fsl) {
        LOG_ERROR("Internal error. Supernode list object missing");
        return Status::Error;
    }

    if (!supernode) {
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
    MINFO("received announce for id: " << announce.supernode_public_id);

    if (fsl->exists(announce.supernode_public_id)) {
        // check if supernode currently busy
        SupernodePtr sn = fsl->get(announce.supernode_public_id);
        if (sn->busy()) {
            MWARNING("Unable to update supernode with announce: " << announce.supernode_public_id << ", BUSY");
            return Status::Error; // we don't care about reply here, already replied to the client
        }
        if (!fsl->get(announce.supernode_public_id)->updateFromAnnounce(announce)) {
            LOG_ERROR("Failed to update supernode with announce: " << announce.supernode_public_id);
            return Status::Error; // we don't care about reply here, already replied to the client
        }
    } else {
        std::string cryptonode_rpc_address = ctx.global["cryptonode_rpc_address"];
        bool testnet = ctx.global["testnet"];

        Supernode * s  = Supernode::createFromAnnounce(announce,
                                                       cryptonode_rpc_address,
                                                       testnet);
        if (!s) {
            LOG_ERROR("Cant create watch-only supernode wallet for id: " << announce.supernode_public_id);
            return Status::Error;

        }

        MINFO("About to add supernode to list [" << s << "]: " << s->idKeyAsString());
        if (!fsl->add(s)) {
            // DO NOT delete "s" here, it will be deleted by smart pointer;
            LOG_ERROR("Can't add new supernode to list [" << s << "]" << s->idKeyAsString());
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

Status sendAnnounce(const graft::Router::vars_t& vars, const graft::Input& input, graft::Context& ctx,
        graft::Output& output)
{
    try {
        switch (ctx.local.getLastStatus()) {
        case graft::Status::Forward: // reply from cryptonode
            return sendOkResponseToCryptonode(output);
        case graft::Status::Error: // failed to send announce
            LOG_ERROR("Failed to send announce");
            return graft::Status::Ok;
        case graft::Status::Ok:
        case graft::Status::None:
            graft::SupernodePtr supernode;

            supernode = ctx.global.get(CONTEXT_KEY_SUPERNODE, graft::SupernodePtr(nullptr));

            if (!supernode.get()) {
                LOG_ERROR("supernode is not set in global context");
                return graft::Status::Error;
            }

            MDEBUG("about to refresh supernode: " << supernode->idKeyAsString());

            if (!supernode->refresh()) {
                return errorCustomError(string("failed to refresh supernode: ") + supernode->idKeyAsString(),
                                        ERROR_INTERNAL_ERROR, output);
            }

            supernode->setLastUpdateTime(static_cast<int64_t>(std::time(nullptr)));

            SendSupernodeAnnounceJsonRpcRequest req;
            if (!supernode->prepareAnnounce(req.params)) {
                return errorCustomError(string("failed to prepare announce: ") + supernode->idKeyAsString(),
                                        ERROR_INTERNAL_ERROR, output);
            }


            req.method = "send_supernode_announce";
            req.id = 0;
            output.load(req);

            output.path = "/json_rpc/rta";
            // DBG: without cryptonode
            // output.path = "/dapi/v2.0/send_supernode_announce";
            MDEBUG("sending announce for id: " << supernode->idKeyAsString());
            MDEBUG(output.data());
            return graft::Status::Forward;
        }
    }
    catch(const std::exception &e)
    {
        LOG_ERROR("Exception thrown: " << e.what());
    }
    catch(...)
    {
        LOG_ERROR("Unknown exception thrown");
    }
    return Status::Ok;
};

}

