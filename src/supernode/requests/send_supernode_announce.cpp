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
#include "supernode/requests/broadcast.h"
#include "supernode/requests/unicast.h"
#include "rta/fullsupernodelist.h"

#include "lib/graft/common/utils.h"
#include <boost/endian/conversion.hpp>

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
        // check if supernode currently busy
        SupernodePtr sn = fsl->get(announce.address);
        if (sn->busy()) {
            MWARNING("Unable to update supernode with announce: " << announce.address << ", BUSY");
            return Status::Error; // we don't care about reply here, already replied to the client
        }
        if (!fsl->get(announce.address)->updateFromAnnounce(announce)) {
            LOG_ERROR("Failed to update supernode with announce: " << announce.address);
            return Status::Error; // we don't care about reply here, already replied to the client
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

            MDEBUG("about to refresh supernode: " << supernode->walletAddress());

            if (!supernode->refresh()) {
                return errorCustomError(string("failed to refresh supernode: ") + supernode->walletAddress(),
                                        ERROR_INTERNAL_ERROR, output);
            }

            supernode->setLastUpdateTime(static_cast<int64_t>(std::time(nullptr)));

            SendSupernodeAnnounceJsonRpcRequest req;
            if (!supernode->prepareAnnounce(req.params)) {
                return errorCustomError(string("failed to prepare announce: ") + supernode->walletAddress(),
                                        ERROR_INTERNAL_ERROR, output);
            }


            req.method = "send_supernode_announce";
            req.id = 0;
            output.load(req);

            output.path = "/json_rpc/rta";
            // DBG: without cryptonode
            // output.path = "/dapi/v2.0/send_supernode_announce";

            MDEBUG("sending announce for address: " << supernode->walletAddress()
                   << ", stake amount: " << supernode->stakeAmount());
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
}

namespace
{

using IdSet = std::vector<std::string>;

void getSupernodesWithStake(graft::Context& ctx, IdSet& allWithStake)
{
    allWithStake.clear();
    ctx.global.apply<boost::shared_ptr<graft::FullSupernodeList>>("fsl",
        [&allWithStake](boost::shared_ptr<graft::FullSupernodeList>& fsl)->bool
    {
        if(!fsl) return false;
        std::vector<std::string> items = fsl->items();
        for(auto& item : items)
        {
            graft::SupernodePtr sptr = fsl->get(item);
            if(!sptr->stakeAmount()) continue;
            allWithStake.emplace_back(item);
        }
    }
    );

    std::sort(allWithStake.begin(), allWithStake.end());
}

//both can be modified
void fillSubsetFromAll(IdSet& all, IdSet& subset, size_t required)
{
    {//remove from subset that are not in all
        IdSet intersection; intersection.reserve(std::min(subset.size(), all.size()));
        std::set_intersection(subset.begin(), subset.end(),
                              all.begin(), all.end(),
                              std::back_inserter(intersection)
                              );
        subset.swap(intersection);
    }

    assert(subset.size() <= required);
    if(required <= subset.size()) return;

    {//remove from all that are in selectedSupernodes
        IdSet diff; diff.reserve(subset.size());
        std::set_difference(all.begin(), all.end(),
                            subset.begin(), subset.end(),
                            std::back_inserter(diff)
                            );
        all.swap(diff);
    }

    //make random subset(add) from all
    IdSet add;
    size_t cnt = required - subset.size();
    if(cnt <= all.size())
    {
        add.swap(all);
    }
    else
    {
        size_t c = std::min(cnt, all.size() - cnt);
        for(size_t i=0; i<c; ++i)
        {
            size_t idx = graft::utils::random_number(size_t(0), all.size()-1);
            auto it = all.begin() + idx;
            add.push_back(*it); all.erase(it);
        }
        if(c<=cnt)
        {
            add.swap(all);
        }
        else
        {
            std::sort(add.begin(), add.end());
        }
    }

    //merge (add) into selectedSupernodes
    size_t middle = subset.size();
    subset.insert(subset.end(), add.begin(), add.end());
    std::inplace_merge(subset.begin(), subset.begin()+middle, subset.end());
}

} // namespace

std::string prepareMyIpBroadcast(graft::Context& ctx)
{
    const size_t selectedCount = 10;

    using IdSet = std::vector<std::string>;
    IdSet allWithStake;
    //get sorted list of all supernodes with stake
    getSupernodesWithStake(ctx, allWithStake);

    if(allWithStake.empty()) return std::string();

    //It is expected that this is the only function that accesses selectedSupernodes
    if(!ctx.global.hasKey("selectedSupernodes")) ctx.global["selectedSupernodes"] = boost::make_shared<IdSet>();
    boost::shared_ptr<IdSet> selectedSupernodes = ctx.global.get("selectedSupernodes", boost::shared_ptr<IdSet>());

    fillSubsetFromAll(allWithStake, *selectedSupernodes, selectedCount);

    if(selectedSupernodes->empty()) return std::string();
    //TODO: call encryptMessage
/*
    //make supernode ID:host:port
    std::string

    void encryptMessage(const std::string& input, const std::vector<crypto::public_key>& Bkeys, std::string& output);

    graft::crypto_tools::encryptMessage()
*/
}

graft::Status periodicUpdateRedirectIds(const graft::Router::vars_t& vars, const graft::Input& input, graft::Context& ctx,
        graft::Output& output)
{
    try {
        switch (ctx.local.getLastStatus()) {
        case graft::Status::Forward: // reply from cryptonode
        {
            int x = ctx.local["updateRedirectIdsState"];
            if(false && !x)
            {
                ctx.local["updateRedirectIdsState"] = 1;

                BroadcastRequestJsonRpc cryptonode_req;
                cryptonode_req.method = "broadcast";
                cryptonode_req.params.callback_uri = "/cryptonode/update_redirect_ids";
                static int i = 0;
                cryptonode_req.params.data = std::string("uuuOOO") + std::to_string(++i);
//                output.load(cryptonode_req);
                output.path = "/json_rpc/rta";
                output.load(cryptonode_req);
                return graft::Status::Forward;
            }
            if(true && !x)
            {
                ctx.local["updateRedirectIdsState"] = 1;

                UnicastRequestJsonRpc req;
                //req.params.sender_address
                req.params.receiver_address = "127.0.0.1";
                req.params.callback_uri = "/my_unicast";
                static int id = graft::utils::random_number(0,100);
                static int i = 0;
                req.params.data = "unicast data " + std::to_string(id) + " " + std::to_string(++i);
                req.params.wait_answer = false;

                req.method = "unicast";
                output.path = "/json_rpc/rta";
                output.load(req);
                return graft::Status::Forward;
            }
/*
            if(x==1)
            {
                ctx.local["updateRedirectIdsState"] = 2;

                output.host = "192.168.5.1";
                return graft::Status::Forward;
            }
            if(x==2)
            {
                ctx.local["updateRedirectIdsState"] = 3;

                output.host = "192.168.5.2";
                return graft::Status::Forward;
            }
*/
            return sendOkResponseToCryptonode(output);
        }
        case graft::Status::Error: // failed to send redirect id
            LOG_ERROR("Failed to send announce");
            return graft::Status::Ok;
        case graft::Status::Ok:
        case graft::Status::None:
        {
            ctx.local["updateRedirectIdsState"] = int(0);

            SupernodeRedirectIdsJsonRpcRequest req;

            req.params.cmd = 0; //add
            req.params.id = "AAABBB"; //my ID

            req.method = "redirect_supernode_id";
            req.id = 0;
            output.load(req);

            output.path = "/json_rpc/rta";
            // DBG: without cryptonode
            // output.path = "/dapi/v2.0/redirect_supernode_id";

            MDEBUG("sending redirect_supernode_id for: ") << req.params.id;
            return graft::Status::Forward;
        }
/*
            graft::SupernodePtr supernode;

            supernode = ctx.global.get(CONTEXT_KEY_SUPERNODE, graft::SupernodePtr(nullptr));

            if (!supernode.get()) {
                LOG_ERROR("supernode is not set in global context");
                return graft::Status::Error;
            }

            MDEBUG("about to refresh supernode: " << supernode->walletAddress());

            if (!supernode->refresh()) {
                return errorCustomError(string("failed to refresh supernode: ") + supernode->walletAddress(),
                                        ERROR_INTERNAL_ERROR, output);
            }

            supernode->setLastUpdateTime(static_cast<int64_t>(std::time(nullptr)));

            SendSupernodeAnnounceJsonRpcRequest req;
            if (!supernode->prepareAnnounce(req.params)) {
                return errorCustomError(string("failed to prepare announce: ") + supernode->walletAddress(),
                                        ERROR_INTERNAL_ERROR, output);
            }


            req.method = "send_supernode_announce";
            req.id = 0;
            output.load(req);

            output.path = "/json_rpc/rta";
            // DBG: without cryptonode
            // output.path = "/dapi/v2.0/send_supernode_announce";

            MDEBUG("sending announce for address: " << supernode->walletAddress()
                   << ", stake amount: " << supernode->stakeAmount());
            return graft::Status::Forward;
*/
        }
    }
    catch(const std::exception &e)
    {
        LOG_ERROR("Exception in updateRedirectIds thrown: " << e.what());
    }
    catch(...)
    {
        LOG_ERROR("Unknown exception in updateRedirectIds thrown");
    }
    return graft::Status::Ok;
}

graft::Status onUpdateRedirectIds(const graft::Router::vars_t& vars, const graft::Input& input, graft::Context& ctx,
        graft::Output& output)
{
    try {
        switch (ctx.local.getLastStatus()) {
        case graft::Status::Ok:
        case graft::Status::None:
        {
            BroadcastRequestJsonRpc req;
            input.get(req);
            MDEBUG("got 111 redirect_supernode_id from '") << input.host << ":" << input.port << "' : " << req.params.data;

            //register others IDs
            SupernodeRedirectIdsJsonRpcRequest sreq;
            sreq.params.cmd = 0; //add
            sreq.params.id = "anOtherID";

            sreq.method = "redirect_supernode_id";
            sreq.id = 0;
            output.load(sreq);
            output.path = "/json_rpc/rta";

            return graft::Status::Forward;
        } break;
        case graft::Status::Forward:
        {
            return graft::Status::Ok;
        } break;
        }
    }
    catch(const std::exception &e)
    {
        LOG_ERROR("Exception in onUpdateRedirectIds thrown: " << e.what());
    }
    catch(...)
    {
        LOG_ERROR("Unknown exception in onUpdateRedirectIds thrown");
    }
    return graft::Status::Ok;
}

graft::Status onMyUnicast_test(const graft::Router::vars_t& vars, const graft::Input& input, graft::Context& ctx, graft::Output& output)
{
    switch (ctx.local.getLastStatus()) {
    case graft::Status::Ok:
    case graft::Status::None:
        MDEBUG("==> onMyUnicast_test");
        return graft::Status::Ok;
    }
}


void registerSendSupernodeAnnounceRequest(graft::Router &router)
{
    std::string endpoint = "/cryptonode/update_redirect_ids";
//    std::string endpoint = "/update_redirect_ids";
    router.addRoute(endpoint, METHOD_POST, {nullptr, onUpdateRedirectIds, nullptr});

    router.addRoute("/my_unicast", METHOD_POST|METHOD_GET, {nullptr, onMyUnicast_test, nullptr});
/*
    Router::Handler3 h3(nullptr, sendSupernodeAnnounceHandler, nullptr);

    router.addRoute(PATH, METHOD_POST, h3);
    LOG_PRINT_L0("route " << PATH << " registered");
*/
}

} //namespace graft::supernode::request

