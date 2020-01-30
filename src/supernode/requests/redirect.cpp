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

#include "supernode/requests/redirect.h"
#include "supernode/requests/broadcast.h"
#include "rta/fullsupernodelist.h"
#include "rta/supernode.h"
#include "lib/graft/common/utils.h"
#include "lib/graft/GraftletLoader.h"
#include <utils/cryptmsg.h>

#include <boost/algorithm/string.hpp>
#include <deque>


#undef MONERO_DEFAULT_LOG_CATEGORY
#define MONERO_DEFAULT_LOG_CATEGORY "supernode.redirect"

namespace graft::supernode::request {

#ifdef UDHT_INFO

namespace
{

using Clock = std::chrono::system_clock;

const Clock::duration time_wnd = std::chrono::hours(12);

struct UdhtStates
{
    //announce expire time, hops left
    using ContTimeHop = std::deque<std::pair<Clock::time_point, uint64_t>>;
    ContTimeHop announces;
    ContTimeHop redirects;
};

class UdhtInfo
{
    void chop_old(UdhtStates* item = nullptr)
    {
        auto edge = Clock::now() - time_wnd;
        auto chop = [&edge](UdhtStates::ContTimeHop& cont)->void
        {
            auto v = std::make_pair(edge, (uint64_t)0);
            auto it = std::upper_bound(cont.begin(), cont.end(), v, [](auto& a, auto& b){ return a.first > b.first; } );
            cont.erase(it, cont.end());
        };
        if(item)
        {
            chop(item->announces);
            chop(item->redirects);
        }
    }
public:
    enum Cont
    {
        Announces,
        Redirects,
        OthersAnnounces,
        OthersRedirects,
    };

    using Id = std::string;
    std::unordered_map<Id, UdhtStates> id2states;
    void add(Id id, uint64_t hops, Cont to_cont)
    {
        assert(!id.empty());
        UdhtStates& item = id2states[id];
        chop_old(&item);
        UdhtStates::ContTimeHop* cont = nullptr;
        switch(to_cont)
        {
        case Announces: cont = &item.announces; break;
        case Redirects: cont = &item.redirects; break;
        default: assert(false);
        }
        cont->push_front(std::make_pair(Clock::now(), hops));
    }

    void chop_all_old()
    {
        chop_old();
        for(auto& item : id2states)
        {
            chop_old(&item.second);
        }
    }

    static std::string local_time(const Clock::time_point& tp)
    {
        auto tm = Clock::to_time_t(tp);

        std::ostringstream ss;
        ss << std::put_time(std::localtime(&tm), "%F %T");
        return ss.str();
    }

    static std::pair<uint64_t, size_t> get_hops(UdhtStates::ContTimeHop& cont)
    {
        if(cont.empty()) return std::make_pair(uint64_t(0),size_t(0));
        uint64_t hops = 0;
        std::for_each(cont.begin(), cont.end(), [&hops]( auto& item ){ hops += item.second; } );
        return std::make_pair(hops, cont.size());
    }

    std::pair<uint64_t, size_t> get_all_hops(Cont from_cont)
    {
        std::pair<uint64_t, size_t> res = std::make_pair(uint64_t(0),size_t(0));
        for(auto& it : id2states)
        {
            auto& item = it.second;
            UdhtStates::ContTimeHop* cont = nullptr;
            switch(from_cont)
            {
            case Announces: cont = &item.announces; break;
            case Redirects: cont = &item.redirects; break;
            default: assert(false);
            }
            auto pair = get_hops(*cont);
            res.first += pair.first;
            res.second += pair.second;
        }
        return res;
    }
};

using UdhtInfoShared = std::shared_ptr<UdhtInfo>;

} //namespace

#endif //UDHT_INFO

namespace
{

static const std::string CONTEXT_KEY_SUPERNODE_PUBKEY("supernode_pubkey"); // key to store own public id(key)
static const std::string CONTEXT_KEY_SUPERNODE_SECKEY("supernode_seckey"); // key to storw own secret key
static const std::string CONTEXT_KEY_SUPERNODE_INITIALIZED("supernode_initialized"); // flag to indicate if supernode initialize
static const std::string CONTEXT_KEY_SUPERNODE_ADDRESS_TABLE("supernode_address_table"); // map of the public id -> network address
static const std::string CONTEXT_KEY_RANDOM_SUPERNODES("random_supernodes"); // map of the public id -> network address

static const std::string REGISTER_SUPERNODE_ENDPOINT("/cryptonode/register_supernode");
static const std::string CRYPTONODE_UPDATE_RTA_ROUTE("/cryptonode/update_rta_route");


using IdSet = std::vector<std::string>;

using ForwardTable = std::map<std::string, std::string>;
using ForwardTablePtr = std::shared_ptr<ForwardTable>;

// TODO: move it to FullSupernodeList 
void getValidSupernodes(graft::Context& ctx, IdSet& result)
{
    result.clear();
    std::string myIDstr;
    const crypto::public_key& pubID = ctx.global[CONTEXT_KEY_SUPERNODE_PUBKEY];
    myIDstr = epee::string_tools::pod_to_hex(pubID);
    
    ctx.global.apply<boost::shared_ptr<graft::FullSupernodeList>>(CONTEXT_KEY_FULLSUPERNODELIST,
        [&result, &myIDstr](boost::shared_ptr<graft::FullSupernodeList>& fsl)->bool
    {
        if (!fsl) {
            MERROR("fsl isn't initialized");
            return false;
        }
        std::vector<std::string> items = fsl->items();
        for (auto& item : items)
        {
            graft::SupernodePtr sptr = fsl->get(item);
            if (sptr->stakeAmount() < Supernode::TIER1_STAKE_AMOUNT || item == myIDstr)
                continue;
            result.emplace_back(item);
        }
        return true;
    }
    );

    std::sort(result.begin(), result.end());
}

//both sets can be modified
void fillSubsetFromAll(IdSet& all, IdSet& subset, size_t required)
{
    // TODO: why not just random subset?
    
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
    if(all.size() <= cnt)
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
        if(c<cnt)
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

bool is_initializedIDs(graft::Context& ctx)
{
    if (!ctx.global.hasKey(CONTEXT_KEY_SUPERNODE_INITIALIZED))
        return false;
    return ctx.global[CONTEXT_KEY_SUPERNODE_INITIALIZED];
    // why not:
    // return ctx.global.hasKey(CONTEXT_KEY_SUPERNODE_INITIALIZED); // TODO: rename
    
}

bool initializeIDs(graft::Context& ctx)
{
    if(ctx.global.hasKey(CONTEXT_KEY_SUPERNODE_INITIALIZED))
    {
        return ctx.global[CONTEXT_KEY_SUPERNODE_INITIALIZED]; //already initialized
    }

    assert(ctx.global.hasKey("jump_node_coefficient"));
    assert(ctx.global.hasKey("redirect_timeout_ms"));

    const double& jump_node_coefficient = ctx.global["jump_node_coefficient"];
    uint32_t broadcast_hops = 1 + std::lround(1./jump_node_coefficient);
    ctx.global["broadcast_hops"] = broadcast_hops;

    //get ID keys
    crypto::secret_key secID;
    crypto::public_key pubID;
    try
    {
        graftlet::GraftletLoader* gloader = ctx.global["graftletLoader"];
        assert(gloader);
        graftlet::GraftletHandler plugin = gloader->buildAndResolveGraftlet("walletAddress");
        using Sign = bool (crypto::public_key& pub, crypto::secret_key& sec);
        bool res = plugin.invoke<Sign>("walletAddressGL.getIdKeys", pubID, secID);
        if(!res) return false; //keys are not valid
    }
    catch(...)
    {
        LOG_ERROR("Cannot find keys graftlet walletAddress walletAddressGL.getIdKeys");
        return false;
    }
    ctx.global[CONTEXT_KEY_SUPERNODE_SECKEY] = secID;
    ctx.global[CONTEXT_KEY_SUPERNODE_PUBKEY] = pubID;

    ctx.global[CONTEXT_KEY_SUPERNODE_ADDRESS_TABLE] = std::make_shared<ForwardTable>();

#ifdef UDHT_INFO
    ctx.global["UdhtInfo"] = std::make_shared<UdhtInfo>();
#endif //UDHT_INFO

    ctx.global[CONTEXT_KEY_SUPERNODE_INITIALIZED] = true;
    return true;
}

#if tst
std::vector<std::string> tst_IDs;
std::string tst_myIDstr;
#endif

std::string buildNetworkAddressMsg(graft::Context& ctx)
{
    assert(ctx.global[CONTEXT_KEY_SUPERNODE_INITIALIZED]);
    
    // XXX: Using plain text broadcasts for now
    const crypto::public_key& public_id = ctx.global[CONTEXT_KEY_SUPERNODE_PUBKEY];

    std::string id = epee::string_tools::pod_to_hex(public_id);
    const std::string& external_address = ctx.global["external_address"];
    std::string plain = id + ':' + external_address;
    // return plain;
    
#if 1
    using IdSet = std::vector<std::string>;
    IdSet allWithStake;
    //get sorted list of all supernodes with stake
    getValidSupernodes(ctx, allWithStake);

    const double& jump_node_coefficient = ctx.global["jump_node_coefficient"];
    size_t selectedCount = std::lround(allWithStake.size() * jump_node_coefficient);

#if tst
    //temporarily, to test
    static bool first = true;
    if(first)
    {//fiction
        first = false;

        for(int i=0; i<0; ++i)
        {
            crypto::secret_key b;
            crypto::public_key B;
            crypto::generate_keys(B, b);
            allWithStake.push_back( epee::string_tools::pod_to_hex(B) );
        }
        std::string myIDstr;
        {
            const crypto::public_key& pubID = ctx.global[CONTEXT_KEY_SUPERNODE_PUBKEY];
            myIDstr = epee::string_tools::pod_to_hex(pubID);
        }
        tst_myIDstr = myIDstr;
        tst_IDs.push_back("7f06eb8659c594c3f654a6814a2fac91c07a3ac711df993da1d00cab1bb4a33b");
        tst_IDs.push_back("cb350e34fa8c8913db902e7210ed55d0fda3f95c5636caa61cbc46d26c42a2e8");
        tst_IDs.push_back("6da61691a3841ddc2e4c02868aba623132953d2d8150dafecc34efacbb54abca");
        tst_IDs.push_back("ab59657817fda347aad9bda78184863737b422f09910089127cf9e6480d83665");
    }

    {
        for(auto& it : tst_IDs)
        {
            if(it != tst_myIDstr) allWithStake.push_back(it);
        }

        std::sort(allWithStake.begin(), allWithStake.end());
        selectedCount = allWithStake.size();
    }
#else
    if(allWithStake.empty()) return std::string();

#endif
    //It is expected that this is the only function that accesses selectedSupernodes
    if (!ctx.global.hasKey(CONTEXT_KEY_RANDOM_SUPERNODES)) 
        ctx.global[CONTEXT_KEY_RANDOM_SUPERNODES] = boost::make_shared<IdSet>();
    
    boost::shared_ptr<IdSet> selectedSupernodes = ctx.global.get(CONTEXT_KEY_RANDOM_SUPERNODES, boost::shared_ptr<IdSet>());

    fillSubsetFromAll(allWithStake, *selectedSupernodes, selectedCount);

    if (selectedSupernodes->empty()) {
      MERROR("Failed to select random supernodes");
      return std::string();
    }

    //encrypt
    std::string message;
    {
        std::vector<crypto::public_key> Bkeys;
        for(auto& Bstr : *selectedSupernodes)
        {
            crypto::public_key B;
            epee::string_tools::hex_to_pod(Bstr, B);
            Bkeys.emplace_back(std::move(B));
        }
#if tst
        {//
            std::ostringstream oss;
            for(auto& k : Bkeys)
            {
                oss << epee::string_tools::pod_to_hex(k) << "\n";
            }
            LOG_PRINT_L0("prepare encrypted message for\n") << oss.str();
        }
#endif
        graft::crypto_tools::encryptMessage(plain, Bkeys, message);
    }

    return message;
#endif // #if 0    
}

} // namespace

//registers the supernode in cryptonode
graft::Status periodicRegisterSupernode(const graft::Router::vars_t& vars, const graft::Input& input, graft::Context& ctx,
        graft::Output& output)
{
    
    // return graft::Status::Ok;
    try {
        switch (ctx.local.getLastStatus()) {
        case graft::Status::Ok:
        case graft::Status::None:
        {
            bool res = initializeIDs(ctx);
            std::string my_pubIDstr;
            if(ctx.global.hasKey(CONTEXT_KEY_SUPERNODE_PUBKEY))
            {
                const crypto::public_key& my_pubID = ctx.global[CONTEXT_KEY_SUPERNODE_PUBKEY];
                my_pubIDstr = epee::string_tools::pod_to_hex(my_pubID);
            }
            LOG_PRINT_L0("my_pubIDstr " << my_pubIDstr);
#if tst
            {
                Id2IpShared map = ctx.global[CONTEXT_KEY_SUPERNODE_ADDRESS_TABLE];
                std::ostringstream oss;
                for(auto& it : *map)
                {
                    oss << it.first << "->" << it.second << "\n";
                }
                LOG_PRINT_L0("known IDs\n") << oss.str();
            }
#endif

            const std::string& supernode_url = ctx.global["supernode_url"]; //[config.ini][server]http_address + "/dapi/v2.0";

            RegisterSupernodeJsonRpcRequest req;

            req.params.broadcast_hops = ctx.global["broadcast_hops"];
            req.params.redirect_timeout_ms = ctx.global["redirect_timeout_ms"];
            req.params.supernode_id = my_pubIDstr; //can be empty if not set
            req.params.supernode_url = supernode_url;
            req.params.redirect_uri = "/redirect_broadcast";

            req.method = "register_supernode";
            req.id = 0;

            output.load(req);
            output.path = "/json_rpc/rta";

            MDEBUG("registering supernode in cryptonode ");
            return graft::Status::Forward;
        }
        case graft::Status::Forward:
        {
            return graft::Status::Ok;
        }
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

// multicasts network address to the randomly selected subset of all known (valid) supernodes. 
graft::Status periodicAnnounceNetworkAddress(const graft::Router::vars_t& /*vars*/, const graft::Input& /*input*/, graft::Context& ctx,
        graft::Output& output)
{
    try {
        switch (ctx.local.getLastStatus()) {
        case graft::Status::Ok:
        case graft::Status::None:
        {
            if(!initializeIDs(ctx))
            {
                MERROR("ID keys are not used. The supernode will not participate in message forwarding.");
                return graft::Status::Stop;
            }

            std::string message = buildNetworkAddressMsg(ctx);
            if (message.empty())  {
               MWARNING("Failed to build address broadcast");
               return graft::Status::Ok;
            }

            BroadcastRequestJsonRpc req;
            req.params.callback_uri = CRYPTONODE_UPDATE_RTA_ROUTE;
            req.params.data = graft::utils::base64_encode(message);
            // XXX: plain-text supenode address
            // req.params.data = message;

            req.method = "wide_broadcast";
            //TODO: do we need unique id?
            static uint64_t i = 0;
            req.id = ++i;

            output.path = "/json_rpc/rta";
            output.load(req);
            return graft::Status::Forward;
        }
        case graft::Status::Forward:
        {
            return graft::Status::Ok;
        }
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


/**
 * @brief onUpdateRtaRoute - updates RTA route path (id -> IP:PORT mapping)
 * @param vars
 * @param input
 * @param ctx
 * @param output
 * @return 
 */
graft::Status onUpdateRtaRoute(const graft::Router::vars_t& vars, const graft::Input& input, graft::Context& ctx,
        graft::Output& output)
{
    graft::Status status = ctx.local.getLastStatus();
    try {
        switch (status) {
        case graft::Status::Ok:
        case graft::Status::None:
        {
            if(!initializeIDs(ctx)) // What?
            {
                MDEBUG("redirect_supernode_id. But does not participate.");
                return graft::Status::Ok;
            }

            BroadcastRequestJsonRpc ireq;
            input.get(ireq);
            MDEBUG("redirect_supernode_id from '" << input.host << ":" << input.port << "' : " << ireq.params.data);

            std::string node_address_str; // TODO: rename as "node_address_str";
            {
                // std::string message = graft::utils::base64_decode(ireq.params.data);
                std::string message = ireq.params.data;
                
#if 1                
                if(!ctx.global.hasKey(CONTEXT_KEY_SUPERNODE_SECKEY))
                {
                    MWARNING("My secret key not found.");
                    return graft::Status::Ok;
                }
                const crypto::secret_key& secID = ctx.global[CONTEXT_KEY_SUPERNODE_SECKEY];
#if tst
                {
                    crypto::public_key P;
                    crypto::secret_key_to_public_key(secID, P);
                    std::string s1 = epee::string_tools::pod_to_hex(P);
                    std::string s2 = tst_myIDstr;
                    assert(s2.empty() || s1 == s2);
                }
#endif
                bool res1 = graft::crypto_tools::decryptMessage(message, secID, node_address_str);
                if(!res1)
                {
                    MDEBUG("The message is not for me.");
                    return sendOkResponseToCryptonode(output);
                }
#endif                 
                node_address_str = message;
            }

            MDEBUG("node address decrypted. " << node_address_str);

            std::string pubkey, network_address;
            {
                std::vector<std::string> tokens;
                boost::algorithm::split(tokens, node_address_str, [](char c) { return c == ':';});
                if (tokens.size() != 3) {
                    MERROR("Invalid node address, expected 'ID:IP:PORT', got: '" << node_address_str << "'");
                    return graft::Status::Ok;    
                }
                pubkey = tokens.at(0);
                network_address = tokens.at(1) + ":" + tokens.at(2);
            }

            std::string my_pubIDstr;
            if(ctx.global.hasKey(CONTEXT_KEY_SUPERNODE_PUBKEY))
            {
                const crypto::public_key& my_pubID = ctx.global[CONTEXT_KEY_SUPERNODE_PUBKEY];
                my_pubIDstr = epee::string_tools::pod_to_hex(my_pubID);
            }
            assert(!my_pubIDstr.empty());
            if (pubkey == my_pubIDstr) {
                MERROR("The message with my ID.");
                return graft::Status::Ok;
            }

            ctx.global.apply<ForwardTablePtr>(CONTEXT_KEY_SUPERNODE_ADDRESS_TABLE, [&](ForwardTablePtr& map)->bool
            {
                map->emplace(pubkey, network_address);
                return true;
            });

#ifdef UDHT_INFO
            ctx.global.apply<UdhtInfoShared>("UdhtInfo", [&](UdhtInfoShared& info)->bool
            {
                info->add(ID, uint64_t(0) - ireq.params.hops, UdhtInfo::Announces);
                return true;
            });
#endif //UDHT_INFO
            // 
            // Inform connected cryptonode so it should forward (or redirects?) broadcast messages addressed to 'pubkey' to this supernode
            // TODO: IK20200128: still not clear why don't store/maintain id -> network address mapping directly on cryptonode?
            SupernodeRedirectIdsJsonRpcRequest oreq;
            oreq.params.id = pubkey;
            oreq.params.my_id = my_pubIDstr;
            oreq.method = "add_rta_route";
            oreq.id = 0;
            ctx.local["oreq"] = oreq;
            return graft::Status::Again;
        } break;
        //   
        case graft::Status::Again:
        {
            SupernodeRedirectIdsJsonRpcRequest oreq = ctx.local["oreq"];
            output.load(oreq);
            output.path = "/json_rpc/rta";
            return graft::Status::Forward;
        } break;
        
        case graft::Status::Forward:
        {
            return sendOkResponseToCryptonode(output);
        } break;
        default:
          MERROR("FIXME: Unhandled status: " << static_cast<size_t>(status));
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
// TODO: IK use 'forward' verb instead of 'redirect' ?
graft::Status onRedirectBroadcast(const graft::Router::vars_t& vars, const graft::Input& input, graft::Context& ctx, graft::Output& output)
{
    
    graft::Status status = ctx.local.getLastStatus();
    switch (status) {
    case graft::Status::Ok:
    case graft::Status::None:
    {
        LOG_PRINT_L0("onRedirectBroadcast : " << input.body);
        RedirectBroadcastJsonRpc req;
        bool res = input.get(req);
        if (!res) {
            MERROR("Failed to parse request: " << input.body);
            return sendOkResponseToCryptonode(output);
        }

        std::string id, network_address;
        // find id and network address of the 
        bool ok = ctx.global.apply(CONTEXT_KEY_SUPERNODE_ADDRESS_TABLE, std::function<bool(ForwardTablePtr&)>( [&](ForwardTablePtr& map)->bool
        {
            auto it = map->find(req.params.receiver_id);
            if(it == map->end()) return false;
            id = it->first;
            network_address = it->second;
            return true;
        }));

        if (!ok)
        {
            MWARNING("Unknown ID: '" << req.params.receiver_id << "'");
            return sendOkResponseToCryptonode(output);
        }

        {//set output.host, output.port
            size_t pos = network_address.find(':');
            output.host = network_address.substr(0, pos);
            if (pos != std::string::npos) // TODO: isn't port always set?
            {
                output.port = network_address.substr(pos+1);
            }
        }

#ifdef UDHT_INFO
        const uint32_t& broadcast_hops = ctx.global["broadcast_hops"];

        ctx.global.apply<UdhtInfoShared>("UdhtInfo", [&](UdhtInfoShared& info)->bool
        {
            info->add(ID, broadcast_hops - req.params.request.hops + 1, UdhtInfo::Redirects);
            return true;
        });

#endif //UDHT_INFO

        output.path = "dapi/v2.0" + req.params.request.callback_uri;

        MDEBUG("Redirect broadcast for supernode id '" << req.params.receiver_id << "' uri:'"
            << output.host << ":" << output.port << "/" << output.path);
#if tst
        {
            assert(!tst_myIDstr.empty());
            std::ostringstream oss;
            oss << "I am (" << tst_myIDstr << ") redirecting directly to "
                << output.host << ":" << output.port << "/" << output.path
                << " for '" << req.params.receiver_id << "'"
                << " original data '" << req.params.request.data << "'";
            req.params.request.data = oss.str();
        }
#endif
        output.load(req.params.request);
#if tst
        LOG_PRINT_L0(" I redirect '") << input.body << "\nas\n" << output.body;
#endif
        return graft::Status::Forward;
    }
    case graft::Status::Forward: // reply to cryptonode
    {
        return sendOkResponseToCryptonode(output);
    }
    default: 
    {
        MERROR("Unhandled status: " << static_cast<size_t>(status));
    }
        
    }
    return graft::Status::Ok;
}

#ifdef UDHT_INFO

graft::Status onGetUDHTInfo(const graft::Router::vars_t& vars, const graft::Input& input, graft::Context& ctx, graft::Output& output)
{
    switch (ctx.local.getLastStatus()) {
    case graft::Status::Ok:
    case graft::Status::None:
    {
        if(!is_initializedIDs(ctx)) return graft::Status::Error;

        std::string my_pubIDstr;
        {
            const crypto::public_key& my_pubID = ctx.global[CONTEXT_KEY_SUPERNODE_PUBKEY];
            my_pubIDstr = epee::string_tools::pod_to_hex(my_pubID);
        }

        UDHTInfoResponse res;
        res.id = my_pubIDstr;
        res.url = (std::string)ctx.global["supernode_url"];
        res.redirect_uri = "/redirect_broadcast";

        //make copy of CONTEXT_KEY_SUPERNODE_ADDRESS_TABLE
        Id2Ip id2ip;
        ctx.global.apply<Id2IpShared>(CONTEXT_KEY_SUPERNODE_ADDRESS_TABLE, [&id2ip](Id2IpShared& ptr)->bool
        {
            id2ip = *ptr;
        });

        uint32_t redirect_timeout_ms = ctx.global["redirect_timeout_ms"];

        ctx.global.apply<UdhtInfoShared>("UdhtInfo", [&](UdhtInfoShared& info)->bool
        {
            info->chop_all_old();

            Clock::time_point tp_now = Clock::now();

            // fill res.items
            for(auto& it : info->id2states)
            {
                std::string id = it.first;
                UdhtStates states = it.second;
                auto it_ip = id2ip.find(id);
                assert(it_ip != id2ip.end());
                if(it_ip == id2ip.end()) continue;
                if(states.announces.empty()) continue;

                UDHTInfoItem res_item;
                res_item.id = it_ip->first;
                res_item.ip_port = it_ip->second;
                {//res_item.expiration_time
                    auto& tp = states.announces.front().first;
                    tp += std::chrono::milliseconds( redirect_timeout_ms );
                    res_item.active = (tp_now <= tp);
                    res_item.expiration_time = UdhtInfo::local_time(tp);
                }
                res_item.broadcast_count = states.announces.size();
                res_item.redirect_count = states.redirects.size();
                {//res_item.avg_hop_ip_broadcast
                    auto pair = UdhtInfo::get_hops(states.announces);
                    res_item.avg_hop_ip_broadcast = (!pair.second)? 0 : double(pair.first)/pair.second;
                }
                {//res_item.avg_hop_redirect
                    auto pair = UdhtInfo::get_hops(states.redirects);
                    res_item.avg_hop_redirect = (!pair.second)? 0 : double(pair.first)/pair.second;
                }

                res.items.emplace_back(res_item);
            }

            {//res.avg_hop_ip_broadcast
                auto pair = info->get_all_hops(UdhtInfo::Announces);
                res.avg_hop_ip_broadcast = (!pair.second)? 0 : double(pair.first)/pair.second;
            }

            {//res.avg_hop_redirect
                auto pair = info->get_all_hops(UdhtInfo::Redirects);
                res.avg_hop_redirect = (!pair.second)? 0 : double(pair.first)/pair.second;
            }

            return true;
        });

        output.load(res);

        return graft::Status::Ok;
    }
    default: assert(false);
    }
}

#endif //UDHT_INFO


#if tst

graft::Status test_Broadcast_IPport(const graft::Router::vars_t& vars, const graft::Input& input, graft::Context& ctx, graft::Output& output)
{
    switch (ctx.local.getLastStatus()) {
    case graft::Status::Ok:
    case graft::Status::None:
    {
        if(input.body.find("I am (") != std::string::npos)
        {
            LOG_PRINT_L0("test_Broadcast_IPport I got direct call  ") << input.body;
        }
        return graft::Status::Ok;
    }
    case graft::Status::Forward:
    {
        return graft::Status::Ok;
    }
    }
}


graft::Status test_startBroadcast(const graft::Router::vars_t& vars, const graft::Input& input, graft::Context& ctx, graft::Output& output)
{
    switch (ctx.local.getLastStatus()) {
    case graft::Status::Ok:
    case graft::Status::None:
    {
        LOG_PRINT_L0(" test_startBroadcast ; send broadcast with my ID, it is expected I will get the request directly ");
        BroadcastRequestJsonRpc req;
        for(auto& it : tst_IDs)
        {
            req.params.receiver_addresses.push_back(it);
        }
        req.params.callback_uri = "/test_Broadcast_IPport";
        req.params.data = "test_startBroadcast data sender:" + tst_myIDstr;

        req.method = "broadcast";
        static int i = 0;
        req.id = ++i;

        output.loadT(req);
        output.path = "/json_rpc/rta";

        return graft::Status::Forward;
    }
    case graft::Status::Forward:
    {
        return graft::Status::Ok;
    }
    }
}

#endif

void registerRedirectRequests(graft::Router &router)
{
    std::string endpoint = CRYPTONODE_UPDATE_RTA_ROUTE; 

    router.addRoute(endpoint, METHOD_POST, {nullptr, onUpdateRtaRoute, nullptr});
    // TODO: rename /redirect_broadcast as '/broadcast' ? NEVER CALLED NOW
    router.addRoute("/redirect_broadcast", METHOD_POST, {nullptr, onRedirectBroadcast, nullptr});
#ifdef UDHT_INFO
    router.addRoute("/get_udht_info", METHOD_POST, {nullptr, onGetUDHTInfo, nullptr});
#endif //UDHT_INFO

#if tst
/*
curl --header "Content-Type: application/json" --request GET --data '{}'  http://localhost:28690/dapi/v2.0/test_startBroadcast
*/
    router.addRoute("/test_startBroadcast", METHOD_GET|METHOD_POST, {nullptr, test_startBroadcast, nullptr});
    router.addRoute("/test_Broadcast_IPport", METHOD_GET|METHOD_POST, {nullptr, test_Broadcast_IPport, nullptr});
#endif

}

} //namespace graft::supernode::request
