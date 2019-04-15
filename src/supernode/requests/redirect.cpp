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
#include "lib/graft/common/utils.h"
#include "lib/graft/GraftletLoader.h"
#include <utils/cryptmsg.h>
#include <deque>

#undef MONERO_DEFAULT_LOG_CATEGORY
#define MONERO_DEFAULT_LOG_CATEGORY "supernode.redirect"

namespace graft::supernode::request {

#ifdef UDHT_INFO

namespace
{

const std::chrono::system_clock::duration time_wnd = std::chrono::hours(12);

struct UdhtStates
{
    //announce expire time, hops left
    using ContTimeHop = std::deque<std::pair<std::chrono::system_clock::time_point, uint64_t>>;
    ContTimeHop announces;
    ContTimeHop redirects;
};

class UdhtInfo
{
    void chop_old(UdhtStates* item = nullptr)
    {
        auto edge = std::chrono::system_clock::now() - time_wnd;
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
        cont->push_front(std::make_pair(std::chrono::system_clock::now(), hops));
    }

    void chop_all_old()
    {
        chop_old();
        for(auto& item : id2states)
        {
            chop_old(&item.second);
        }
    }

    static std::string local_time(const std::chrono::system_clock::time_point& tp)
    {
        auto tm = std::chrono::system_clock::to_time_t(tp);

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

using IdSet = std::vector<std::string>;

using Id2Ip = std::map<std::string,std::string>;
using Id2IpShared = std::shared_ptr<std::map<std::string,std::string>>;

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
        return true;
    }
    );

    std::sort(allWithStake.begin(), allWithStake.end());

    {//remove my ID
        std::string myIDstr;
        {
            const crypto::public_key& pubID = ctx.global["my_pubID"];
            myIDstr = epee::string_tools::pod_to_hex(pubID);
        }
        assert(!myIDstr.empty());
        auto rng = std::equal_range(allWithStake.begin(), allWithStake.end(), myIDstr);
        allWithStake.erase(rng.first, rng.second);
    }
}

//both sets can be modified
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
    if(!ctx.global.hasKey("my_ID_initialized")) return false;
    return ctx.global["my_ID_initialized"];
}

bool initializeIDs(graft::Context& ctx)
{
    if(ctx.global.hasKey("my_ID_initialized"))
    {
        return ctx.global["my_ID_initialized"]; //already initialized
    }

    ctx.global["my_ID_initialized"] = false;

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
    ctx.global["my_secID"] = secID;
    ctx.global["my_pubID"] = pubID;

    ctx.global["ID:IP:port map"] = std::make_shared<Id2Ip>();

#ifdef UDHT_INFO
    ctx.global["UdhtInfo"] = std::make_shared<UdhtInfo>();
#endif //UDHT_INFO

    ctx.global["my_ID_initialized"] = true;
    return true;
}

#if tst
std::vector<std::string> tst_IDs;
std::string tst_myIDstr;
#endif

std::string prepareMyIpBroadcast(graft::Context& ctx)
{
    assert((bool)ctx.global["my_ID_initialized"] == true);

    using IdSet = std::vector<std::string>;
    IdSet allWithStake;
    //get sorted list of all supernodes with stake
    getSupernodesWithStake(ctx, allWithStake);

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
            const crypto::public_key& pubID = ctx.global["my_pubID"];
            myIDstr = epee::string_tools::pod_to_hex(pubID);
        }
        tst_myIDstr = myIDstr;
        tst_IDs.push_back("c2e3c8e7adf55ac6be9b9ac62e2cf96b239299b9b3a6ac152fbe4de121188452");
        tst_IDs.push_back("a638e97d174c4f7af32f613a485ceec22394486ac3ed7d8720129f3c40f4260e");
        tst_IDs.push_back("3cad1a7a0b34bba7def142666d454dbc6690624bb643f12d26cc69525591615a");
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
    if(!ctx.global.hasKey("selectedSupernodes")) ctx.global["selectedSupernodes"] = boost::make_shared<IdSet>();
    boost::shared_ptr<IdSet> selectedSupernodes = ctx.global.get("selectedSupernodes", boost::shared_ptr<IdSet>());

    fillSubsetFromAll(allWithStake, *selectedSupernodes, selectedCount);

    if(selectedSupernodes->empty()) return std::string();

    const crypto::public_key& pubID = ctx.global["my_pubID"];

    std::string ID = epee::string_tools::pod_to_hex(pubID);
    const std::string& external_address = ctx.global["external_address"];
    std::string plain = ID + ':' + external_address;

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
}

} // namespace

//registers the supernode in cryptonode
graft::Status periodicRegisterSupernode(const graft::Router::vars_t& vars, const graft::Input& input, graft::Context& ctx,
        graft::Output& output)
{
    try {
        switch (ctx.local.getLastStatus()) {
        case graft::Status::Ok:
        case graft::Status::None:
        {
            bool res = initializeIDs(ctx);

            std::string my_pubIDstr;
            if(ctx.global.hasKey("my_pubID"))
            {
                const crypto::public_key& my_pubID = ctx.global["my_pubID"];
                my_pubIDstr = epee::string_tools::pod_to_hex(my_pubID);
            }
            LOG_PRINT_L0("my_pubIDstr ") << my_pubIDstr;
#if tst
            {
                Id2IpShared map = ctx.global["ID:IP:port map"];
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

//sends encrypted message for random subset of supernodes with stake
graft::Status periodicUpdateRedirectIds(const graft::Router::vars_t& vars, const graft::Input& input, graft::Context& ctx,
        graft::Output& output)
{
    try {
        switch (ctx.local.getLastStatus()) {
        case graft::Status::Ok:
        case graft::Status::None:
        {
            if(!initializeIDs(ctx))
            {
                LOG_PRINT_L0("ID keys are not used. The supernode will not participate in IP redirection.");
                return graft::Status::Stop;
            }

            std::string message = prepareMyIpBroadcast(ctx);
            if(message.empty()) return graft::Status::Ok;

            BroadcastRequestJsonRpc req;
            req.params.callback_uri = "/cryptonode/update_redirect_ids";
            req.params.data = graft::utils::base64_encode(message);

            req.method = "wide_broadcast";
            //TODO: do we need unique id?
            static int i = 0;
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

graft::Status onUpdateRedirectIds(const graft::Router::vars_t& vars, const graft::Input& input, graft::Context& ctx,
        graft::Output& output)
{
    try {
        switch (ctx.local.getLastStatus()) {
        case graft::Status::Ok:
        case graft::Status::None:
        {
            if(!initializeIDs(ctx))
            {
                MDEBUG("redirect_supernode_id. But does not participate.");
                return graft::Status::Ok;
            }

            BroadcastRequestJsonRpc ireq;
            input.get(ireq);
            MDEBUG("redirect_supernode_id from '") << input.host << ":" << input.port << "' : " << ireq.params.data;

            std::string ID_ip_port;
            {
                std::string message = graft::utils::base64_decode(ireq.params.data);
                if(!ctx.global.hasKey("my_secID"))
                {
                    MDEBUG("Secret key not found.");
                    return graft::Status::Ok;
                }
                const crypto::secret_key& secID = ctx.global["my_secID"];
#if tst
                {
                    crypto::public_key P;
                    crypto::secret_key_to_public_key(secID, P);
                    std::string s1 = epee::string_tools::pod_to_hex(P);
                    std::string s2 = tst_myIDstr;
                    assert(s2.empty() || s1 == s2);
                }
#endif
                bool res1 = graft::crypto_tools::decryptMessage(message, secID, ID_ip_port);
                if(!res1)
                {
                    MDEBUG("The message is not for me.");
                    return graft::Status::Ok;
                }
            }

            MDEBUG(" ID:IP:port decrypted. ") << ID_ip_port;

            std::string ID, ip_port;
            {
                size_t pos = ID_ip_port.find(':');
                if(pos == std::string::npos)
                {
                    LOG_ERROR("Invalid format ID:IP:port expected in '") << ID_ip_port << "'";
                    return graft::Status::Error;
                }
                ID = ID_ip_port.substr(0,pos);
                ip_port = ID_ip_port.substr(pos+1);
            }

            std::string my_pubIDstr;
            if(ctx.global.hasKey("my_pubID"))
            {
                const crypto::public_key& my_pubID = ctx.global["my_pubID"];
                my_pubIDstr = epee::string_tools::pod_to_hex(my_pubID);
            }
            assert(!my_pubIDstr.empty());
            if(ID == my_pubIDstr)
            {
                MDEBUG("The message with my ID.");
                return graft::Status::Ok;
            }

            ctx.global.apply<Id2IpShared>("ID:IP:port map", [&](Id2IpShared& map)->bool
            {
                map->emplace(ID, ip_port);
                return true;
            });

#ifdef UDHT_INFO
            ctx.global.apply<UdhtInfoShared>("UdhtInfo", [&](UdhtInfoShared& info)->bool
            {
                info->add(ID, uint64_t(0) - ireq.params.hops, UdhtInfo::Announces);
                return true;
            });
#endif //UDHT_INFO
            //register ID of another supernode to redirect
            SupernodeRedirectIdsJsonRpcRequest oreq;
            oreq.params.id = ID;

            oreq.method = "redirect_supernode_id";
            oreq.id = 0;
            output.load(oreq);
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

graft::Status onRedirectBroadcast(const graft::Router::vars_t& vars, const graft::Input& input, graft::Context& ctx, graft::Output& output)
{
    switch (ctx.local.getLastStatus()) {
    case graft::Status::Ok:
    case graft::Status::None:
    {
        LOG_PRINT_L0("onRedirectBroadcast : ") << input.body;
        RedirectBroadcastJsonRpc req;
        bool res = input.get(req);
        assert(res);

        std::string ID, ip_port;

        bool ok = ctx.global.apply("ID:IP:port map", std::function<bool(Id2IpShared&)>( [&](Id2IpShared& map)->bool
        {
            auto it = map->find(req.params.receiver_id);
            if(it == map->end()) return false;
            ID = it->first;
            ip_port = it->second;
            return true;
        }));

        if(!ok)
        {
            LOG_ERROR("Cannot find supernode IP:port to redirect by ID:'") << req.params.receiver_id << "'";
            return graft::Status::Error;
        }

        {//set output.host, output.port
            int pos = ip_port.find(':');
            output.host = ip_port.substr(0, pos);
            if(pos != std::string::npos)
            {
                output.port = ip_port.substr(pos+1);
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

        MDEBUG("Redirect broadcast for supernode id '") << req.params.receiver_id << "' uri:'"
            << output.host << ":" << output.port << "/" << output.path;
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
    case graft::Status::Forward:
    {
        return graft::Status::Ok;
    }
    }
    assert(false);
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
            const crypto::public_key& my_pubID = ctx.global["my_pubID"];
            my_pubIDstr = epee::string_tools::pod_to_hex(my_pubID);
        }

        UDHTInfoResponse res;
        res.id = my_pubIDstr;
        res.url = (std::string)ctx.global["supernode_url"];
        res.redirect_uri = "/redirect_broadcast";

        Id2IpShared id2ip = ctx.global["ID:IP:port map"];

        ctx.global.apply<UdhtInfoShared>("UdhtInfo", [&](UdhtInfoShared& info)->bool
        {
            info->chop_all_old();

            // fill res.items
            for(auto& it : info->id2states)
            {
                std::string id = it.first;
                UdhtStates states = it.second;
                auto it_ip = id2ip->find(id);
                assert(it_ip != id2ip->end());
                if(it_ip == id2ip->end()) continue;
                if(states.announces.empty()) continue;

                UDHTInfoItem res_item;
                res_item.id = it_ip->first;
                res_item.ip_port = it_ip->second;
                {//res_item.expiration_time
                    auto& tp = states.announces.front().first;
                    uint32_t redirect_timeout_ms = ctx.global["redirect_timeout_ms"];
                    tp += std::chrono::milliseconds( redirect_timeout_ms );
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
    std::string endpoint = "/cryptonode/update_redirect_ids";

    router.addRoute(endpoint, METHOD_POST, {nullptr, onUpdateRedirectIds, nullptr});
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

