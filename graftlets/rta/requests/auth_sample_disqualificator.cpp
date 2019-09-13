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

#include "auth_sample_disqualificator.h"
#include "supernode/requests/disqualificator.h"
#include "rta/fullsupernodelist.h"
#include "supernode/requests/broadcast.h"
#include "lib/graft/binary_serialize.h"
#include "lib/graft/handler_api.h"

#include <misc_log_ex.h>

#include <wallet/wallet2.h>

#include <mutex>
#include <map>

namespace graft::supernode::request
{

namespace
{

GRAFT_DEFINE_IO_STRUCT(DisqualificationItem2,
                       (std::string, payment_id),
                       (uint64_t, block_height),
                       (crypto::hash, block_hash),
                       (std::vector<crypto::public_key>, ids) //sorted, the order is important to get valid signs
                       );

GRAFT_DEFINE_IO_STRUCT(SignerItem,
                       (crypto::public_key, signer_id),
                       (crypto::signature, sign)
                       );

GRAFT_DEFINE_IO_STRUCT(Disqualification2,
                       (DisqualificationItem2, item),
                       (std::vector<SignerItem>, signers)
                       );

GRAFT_DEFINE_IO_STRUCT(Vote,
                       (std::string, payment_id),
                       (SignerItem, si)
                       );

} //namespace

namespace
{

class AuthSDisqualificatorImpl : public AuthSDisqualificator, public BBLDisqualificatorBase
{
    static constexpr size_t BLOCKCHAIN_BASED_LIST_DELAY_BLOCK_COUNT = graft::FullSupernodeList::BLOCKCHAIN_BASED_LIST_DELAY_BLOCK_COUNT;
    static constexpr int32_t REQUIRED_AUTHS_DISQULIFICATION_VOTES = 5;
    static constexpr int32_t VOTING_TIMEOUT_MS = 5000;

    using PaymentId = std::string;

    struct Collection
    {
        Disqualification2 di;
        std::string item_str;
        std::vector<crypto::public_key> auths;
    };

    std::map<PaymentId, Collection> m_map;

    crypto::public_key m_supernode_id;
    crypto::secret_key m_secret_key;
    std::string m_supernode_str_id;

    template<typename T>
    struct less_mem
    {
        static_assert(std::is_trivially_copyable<T>::value);
        bool operator() (const T& l, const T& r)
        {
            int res = std::memcmp(&l, &r, sizeof(T));
            return (res<0);
        }
    };

///////////////////// tools return error message if any
///
    static graft::Status setError(graft::Context& ctx, const std::string& msg, int code = 0)
    {
        count_errors(msg, code);
        LOG_ERROR(msg);
        ctx.local.setError(msg.c_str(), graft::Status::Error);
        return graft::Status::Error;
    };

    template<typename T>
    static void bin_serialize(const T& t, std::string& str)
    {
        graft::Output out;
        out.loadT<graft::serializer::Binary>(t);
        str = out.body;
    }

    template<typename T>
    static std::string bin_deserialize(const std::string& str, T& t)
    {
        graft::Input in;
        in.body = str;
        try
        {
            in.getT<graft::serializer::Binary>(t);
        }
        catch(std::exception& ex)
        {
            return ex.what();
        }
        return "";
    }

    void sign(const std::string& str, crypto::signature& sig)
    {
        crypto::hash hash;
        crypto::cn_fast_hash(str.data(), str.size(), hash);
        crypto::generate_signature(hash, m_supernode_id, m_secret_key, sig);
    }

    static bool check_sign(const std::string& str, const crypto::public_key id, const crypto::signature& sig)
    {
        crypto::hash hash;
        crypto::cn_fast_hash(str.data(), str.size(), hash);
        return crypto::check_signature(hash, id, sig);
    }

//////////

    void getSupenodeKeys(graft::Context& ctx, crypto::public_key& pub, crypto::secret_key& sec)
    {
        if(fnGetSupernodeKeys)
        {
            fnGetSupernodeKeys(pub, sec);
            return;
        }
        //get m_supernode_id & m_supernode_str_id & m_secret_key
        ctx.global.apply<graft::SupernodePtr>("supernode",
            [&pub,&sec](graft::SupernodePtr& supernode)->bool
        {
            assert(supernode);
            pub = supernode->idKey();
            sec = supernode->secretKey();
            return true;
        });
    }

    bool getAuthSample(graft::Context& ctx, uint64_t& block_height, const std::string& payment_id, crypto::hash& block_hash, std::vector<crypto::public_key>& auths)
    {
        if(fnGetAuthSample)
        {
            uint64_t tmp_block_number = block_height + BLOCKCHAIN_BASED_LIST_DELAY_BLOCK_COUNT;
            fnGetAuthSample(tmp_block_number, payment_id, block_hash, auths);
            assert(tmp_block_number == block_height);
            block_height = tmp_block_number;
            return true;
        }
        graft::FullSupernodeList::supernode_array suAuthS;
        bool res = ctx.global.apply<boost::shared_ptr<graft::FullSupernodeList>>("fsl",
            [&](boost::shared_ptr<graft::FullSupernodeList>& fsl)->bool
        {
            if(!fsl) return false;
            uint64_t tmp_block_number;
            bool res = fsl->buildAuthSample(block_height+BLOCKCHAIN_BASED_LIST_DELAY_BLOCK_COUNT, payment_id, suAuthS, tmp_block_number);
            if(!res) return false; //block_height can be old
            assert(tmp_block_number == block_height);
            block_height = tmp_block_number;
            std::string block_hash_str;
            fsl->getBlockHash(block_height, block_hash_str);
            bool res1 = epee::string_tools::hex_to_pod(block_hash_str, block_hash);
            assert(res1);
            return true;
        });
        if(!res) return false;

        auths.clear();
        auths.reserve(suAuthS.size());
        for(auto& item : suAuthS)
        {
            auths.push_back(item->idKey());
        }
    }

/////////// phases
protected:
    graft::Status initDisqualify(graft::Context& ctx, const std::string& payment_id)
    {
        auto it = m_map.find(payment_id);
        assert(it == m_map.end());
        if(it != m_map.end())
        {
            std::ostringstream oss; oss << " in initDisqualify payment id " << payment_id << " already known";
            return setError(ctx, oss.str());
        }
        m_map[payment_id];
        return graft::Status::Ok;
    }

    graft::Status startDisqualify(graft::Context& ctx, const std::string& payment_id, uint64_t block_height, const Ids& ids, graft::Output& output)
    {
        auto it = m_map.find(payment_id);
        assert(it != m_map.end());
        if(it == m_map.end())
        {
            std::ostringstream oss; oss << " in startDisqualify payment id " << payment_id << " unknown";
            return setError(ctx, oss.str());
        }

        if(ids.empty())
        {
            m_map.erase(it);
            return graft::Status::Ok;
        }

        if(m_supernode_str_id.empty())
        {//get m_supernode_id & m_supernode_str_id & m_secret_key, once
            getSupenodeKeys(ctx, m_supernode_id, m_secret_key);
            m_supernode_str_id = epee::string_tools::pod_to_hex(m_supernode_id);
        }

        //fill collection
        Collection& coll = it->second;
        assert(coll.di.item.payment_id.empty());
        assert(coll.item_str.empty());
        coll.di.item.payment_id = payment_id;
        coll.di.item.block_height = block_height;
        coll.di.item.ids = ids;
        std::sort(coll.di.item.ids.begin(), coll.di.item.ids.end(), less_mem<crypto::public_key>{});
        getAuthSample(ctx, block_height, payment_id, coll.di.item.block_hash, coll.auths);
        std::sort(coll.auths.begin(), coll.auths.end(), less_mem<crypto::public_key>{});
        {//remove me from auth sample
            auto pair = std::equal_range(coll.auths.begin(), coll.auths.end(), m_supernode_id, less_mem<crypto::public_key>{});
            assert(1 == std::distance(pair.first, pair.second));
            coll.auths.erase(pair.first);
        }
        //bin serialize coll.di.item
        bin_serialize(coll.di.item, coll.item_str);

        //check already recieved signs
        coll.di.signers.erase(std::remove_if(coll.di.signers.begin(), coll.di.signers.end(),
            [&coll](auto& v)->bool
            {
                auto& [id, sign] = v;
                if(!std::binary_search(coll.auths.cbegin(), coll.auths.cend(), id, less_mem<crypto::public_key>{} ))
                    return true; //means remove
                return !check_sign(coll.item_str, id, sign); //means remove
            }), coll.di.signers.end());

        Vote v;
        v.payment_id = payment_id;
        {//add my sign
            SignerItem& si = v.si;
            si.signer_id = m_supernode_id;
            sign(coll.item_str, si.sign);
            coll.di.signers.emplace_back( si );
        }

        //serialize
        std::string v_str;
        bin_serialize(v, v_str);

        //encrypt
        std::string message;
        graft::crypto_tools::encryptMessage(v_str, coll.auths, message);

        //multicast vote to the auth sample
        BroadcastRequestJsonRpc req;
        for(auto& id : coll.auths)
        {
            req.params.receiver_addresses.push_back(epee::string_tools::pod_to_hex(id));
        }
        req.method = "broadcast";
        req.params.callback_uri = ROUTE_VOTES;
        req.params.data = graft::utils::base64_encode(message);
        req.params.sender_address = m_supernode_str_id;

        output.load(req);
        output.path = "/json_rpc/rta";
        MDEBUG("multicasting: " << output.data());
        MDEBUG(__FUNCTION__ << " end");

        auto onTimeout = [this,it](const graft::Router::vars_t& vars, const graft::Input& input, graft::Context& ctx, graft::Output& output)->graft::Status
        {
            std::lock_guard lk(m_mutex);

            Collection& coll = it->second;
            bool enough = (REQUIRED_AUTHS_DISQULIFICATION_VOTES <= coll.di.signers.size());
            //TODO: remove excess votes to minimize transaction
            if(enough)
            {
                //create extra from disqualification2
                std::vector<uint8_t> extra;
                extra.push_back(TX_EXTRA_GRAFT_DISQUALIFICATION_TAG);
                std::string di_str;
                bin_serialize(coll.di, di_str);
                static_assert(sizeof(di_str[0]) == sizeof(uint8_t));
                std::copy(di_str.begin(), di_str.end(), std::back_inserter(extra));

                //create transaction from extra
                tools::wallet2::pending_tx ptx;
                {
                    cryptonote::transaction& tx = ptx.tx;
                    tx.version = 124;
                    tx.extra = extra;

                    crypto::public_key tmp;
                    crypto::generate_keys(tmp, ptx.tx_key);

                    ptx.construction_data.extra = tx.extra;
                    ptx.construction_data.unlock_time = 0; //unlock_time;
                    ptx.construction_data.use_rct = false;
                }

                if(fnCollectTxs)
                {
                    std::vector<tools::wallet2::pending_tx> txes { ptx };
                    fnCollectTxs(&txes);
                }
                else
                {
                    //create wallet
                    std::string addr = ctx.global["cryptonode_rpc_address"];
                    bool testnet = ctx.global["testnet"];
                    tools::wallet2 wallet(testnet? cryptonote::TESTNET : cryptonote::MAINNET);
                    wallet.init(addr);
                    wallet.set_refresh_from_block_height(coll.di.item.block_height);
                    wallet.set_seed_language("English");

                    try
                    {
                        wallet.commit_tx(ptx);
                    }
                    catch(std::exception& ex)
                    {
                        return setError(ctx, ex.what());
                    }
                }
            }

            m_map.erase(it);

            return graft::Status::Stop;
        };

        bool enough = (REQUIRED_AUTHS_DISQULIFICATION_VOTES <= coll.di.signers.size());

        if(fnAddPeriodic)
        {
            fnAddPeriodic( onTimeout, std::chrono::milliseconds(0), enough? std::chrono::milliseconds(1) : addPeriodic_interval_ms, 0 );
        }
        else
        {
            assert(ctx.handlerAPI());
            ctx.handlerAPI()->addPeriodicTask( onTimeout, std::chrono::milliseconds(0), enough? std::chrono::milliseconds(1) : std::chrono::milliseconds(VOTING_TIMEOUT_MS), 0 );
        }

        return graft::Status::Forward;
    }

    graft::Status handleVotes(const graft::Router::vars_t& vars, const graft::Input& input, graft::Context& ctx, graft::Output& output)
    {
        BroadcastRequestJsonRpc req;
        bool res1 = input.get(req);
        if(!res1)
        {
            return setError(ctx, "cannot deserialize MulticastRequestJsonRpc");
        }

        std::string message = graft::utils::base64_decode(req.params.data);
        std::string plain;
        bool res2 = graft::crypto_tools::decryptMessage(message, m_secret_key, plain);
        if(!res2)
        {
            return setError(ctx, "cannot decrypt, the message is not for me");
        }

        Vote v;
        std::string err = bin_deserialize(plain, v);
        if(!err.empty())
        {
            std::ostringstream oss;
            oss << "cannot deserialize Vote, error: '" << err << "'";
            return setError(ctx, oss.str());
        }

        auto it = m_map.find(v.payment_id);
        if(it == m_map.end())
        {
            std::ostringstream oss; oss << "in " << __FUNCTION__ << " cannot find collection with payment id "
                << v.payment_id << ". Probably such transaction already has been processed or is not started yet.";
            return setError(ctx, oss.str(), 2001);
        }

        Collection& coll = it->second;

        //check vote
        if(!std::binary_search(coll.auths.cbegin(), coll.auths.cend(), v.si.signer_id, less_mem<crypto::public_key>{} ))
        {
            std::ostringstream oss; oss << " signer " << v.si.signer_id
                << " is not in the auth sample with payment id "<< v.payment_id;
            return setError(ctx, oss.str());
        }
        if(!check_sign(coll.item_str, v.si.signer_id, v.si.sign))
        {
            std::ostringstream oss; oss << " invalid signature in vote of " << v.si.signer_id
                << " for payment id "<< v.payment_id << ". Probably it is voting for different candidates. ";
            return setError(ctx, oss.str());
        }

        //add vote
        coll.di.signers.emplace_back( std::move(v.si) );

        return graft::Status::Ok;
    }

    static std::mutex m_mutex;

public:
    //supernodes ids
    using Ids = std::vector<crypto::public_key>;

    static graft::Status staticInitDisqualify(graft::Context& ctx, const std::string& payment_id)
    {
        std::lock_guard<std::mutex> lk(m_mutex);

        if(!ctx.global.hasKey("AuthSDisqualificatorImpl"))
        {
            std::shared_ptr<AuthSDisqualificatorImpl> asd = std::make_shared<AuthSDisqualificatorImpl>();
            ctx.global["AuthSDisqualificatorImpl"] = asd;
        }
        std::shared_ptr<AuthSDisqualificatorImpl> asd = ctx.global["AuthSDisqualificatorImpl"];
        return asd->initDisqualify(ctx, payment_id);
    }

    static graft::Status staticStartDisqualify(graft::Context& ctx, const std::string& payment_id, uint64_t block_height, const Ids& ids, graft::Output& output)
    {
        std::lock_guard<std::mutex> lk(m_mutex);

        if(!ctx.global.hasKey("AuthSDisqualificatorImpl")) return graft::Status::Ok;
        std::shared_ptr<AuthSDisqualificatorImpl> asd = ctx.global["AuthSDisqualificatorImpl"];
        return asd->startDisqualify(ctx, payment_id, block_height, ids, output);
    }

    static graft::Status staticHandleVotes(const graft::Router::vars_t& vars, const graft::Input& input, graft::Context& ctx, graft::Output& output)
    {
        std::lock_guard<std::mutex> lk(m_mutex);

        if(!ctx.global.hasKey("AuthSDisqualificatorImpl")) return graft::Status::Error;
        std::shared_ptr<AuthSDisqualificatorImpl> asd = ctx.global["AuthSDisqualificatorImpl"];
        return asd->handleVotes(vars, input, ctx, output);
    }

    static constexpr const char* ROUTE_VOTES = "/supernode/disqualify2/votes";

};

std::mutex AuthSDisqualificatorImpl::m_mutex;

} //namespace

graft::Status AuthSDisqualificator::initDisqualify(graft::Context& ctx, const std::string& payment_id)
{
    return AuthSDisqualificatorImpl::staticInitDisqualify(ctx, payment_id);
}

graft::Status AuthSDisqualificator::startDisqualify(graft::Context& ctx, const std::string& payment_id, uint64_t block_height, const Ids& ids, graft::Output& output)
{
    return AuthSDisqualificatorImpl::staticStartDisqualify(ctx, payment_id, block_height, ids, output);
}

void registerAuthSDisqualificatorRequest(graft::Router &router)
{
    router.addRoute(AuthSDisqualificatorImpl::ROUTE_VOTES, METHOD_POST, {nullptr, AuthSDisqualificatorImpl::staticHandleVotes , nullptr});
}




namespace
{

class AuthSDisqualificatorTest : public AuthSDisqualificatorImpl
{
    graft::GlobalContextMap globalContextMap;
    graft::Context ctx = graft::Context(globalContextMap);
    virtual void process_command(const command& cmd, std::vector<crypto::public_key>& forward, std::string& body, std::string& callback_uri) override
    {
        assert(!cmd.payment_id.empty());

        graft::Status res;
        graft::Output output;
        if(cmd.uri.empty())
        {
            if(cmd.block_height == 0)
            {
                res = testInitDisqualify(ctx, cmd.payment_id);
                assert(res == graft::Status::Ok);
            }
            else
            {
                res = testStartDisqualify(ctx, cmd.payment_id, cmd.block_height, cmd.ids, output);
                assert(cmd.ids.empty() && res == graft::Status::Ok || !cmd.ids.empty() && res == graft::Status::Forward);
            }
        }
        else
        {
            graft::Input in; in.body = cmd.body;
            assert(cmd.uri == ROUTE_VOTES);
            res = testHandleVotes({}, in, ctx, output);
            assert(cmd.ids.empty() || res == graft::Status::Ok || !cmd.ids.empty() && res == graft::Status::Forward
                   || res == graft::Status::Error);
        }
        if(res == graft::Status::Forward)
        {
            body = output.body;
            BroadcastRequestJsonRpc req;
            graft::Input in; in.body = output.body; in.getT(req);
            callback_uri = req.params.callback_uri;
            for(auto& id_str : req.params.receiver_addresses)
            {
                crypto::public_key id; epee::string_tools::hex_to_pod(id_str, id);
                forward.push_back(std::move(id));
            }
        }
        else
        {
            forward.clear(); body.clear(); callback_uri.clear();
        }
    }

///// tests

    graft::Status testInitDisqualify(graft::Context& ctx, const std::string& payment_id)
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        return initDisqualify(ctx, payment_id);
    }

    graft::Status testStartDisqualify(graft::Context& ctx, const std::string& payment_id, uint64_t block_height, const Ids& ids, graft::Output& output)
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        return startDisqualify(ctx, payment_id, block_height, ids, output);
    }

    graft::Status testHandleVotes(const graft::Router::vars_t& vars, const graft::Input& input, graft::Context& ctx, graft::Output& output)
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        return handleVotes(vars, input, ctx, output);
    }

/////

public:
    static std::mutex m_err_mutex;
    static std::map<int, int> m_errors;
    static std::vector<std::thread> m_threads;

    AuthSDisqualificatorTest()
    {
        ctx.global["fsl"] = nullptr;
    }
};

std::mutex AuthSDisqualificatorTest::m_err_mutex;
std::map<int, int> AuthSDisqualificatorTest::m_errors;
std::vector<std::thread> AuthSDisqualificatorTest::m_threads;

} //namespace

std::unique_ptr<BBLDisqualificatorBase> BBLDisqualificatorBase::createTestAuthSDisqualificator(
    GetSupernodeKeys fnGetSupernodeKeys,
    GetAuthSample fnGetAuthSample,
    CollectTxs fnCollectTxs,
    std::chrono::milliseconds addPeriodic_interval_ms
)
{
    auto addPeriodic = [&](const graft::Router::Handler& h_worker, std::chrono::milliseconds interval_ms, std::chrono::milliseconds initial_interval_ms, double random_factor)->bool
    {
        const graft::Router::Handler worker = h_worker;
        std::thread th( [worker, initial_interval_ms]()->void
        {
            std::this_thread::sleep_for(initial_interval_ms);
            graft::Input input;
            graft::GlobalContextMap globalContextMap;
            graft::Context ctx = graft::Context(globalContextMap);
            graft::Output output;
            graft::Status res = worker({}, input, ctx, output);
            assert(res == graft::Status::Stop);
        } );
//        th.detach();
        AuthSDisqualificatorTest::m_threads.emplace_back(std::move(th));
        return true;
    };

    auto disq = std::make_unique<AuthSDisqualificatorTest>();
    disq->fnGetSupernodeKeys = fnGetSupernodeKeys;
    disq->fnGetAuthSample = fnGetAuthSample;
    disq->fnCollectTxs = fnCollectTxs;
    disq->fnAddPeriodic = addPeriodic;
    disq->addPeriodic_interval_ms = addPeriodic_interval_ms;
    return disq;
}

void BBLDisqualificatorBase::waitForTestAuthSDisqualificator()
{
    for(auto& th : AuthSDisqualificatorTest::m_threads)
    {
        th.join();
    }
    AuthSDisqualificatorTest::m_threads.clear();
}

Status votesHandlerV2(const graft::Router::vars_t &vars, const Input &input, Context &ctx, Output &output)
{
    return AuthSDisqualificatorImpl::staticHandleVotes(vars, input, ctx, output);
}

} //namespace graft::supernode::request
