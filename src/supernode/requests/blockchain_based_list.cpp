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

#include "supernode/requests/blockchain_based_list.h"
#include "supernode/requestdefines.h"
#include "supernode/requests/multicast.h"
#include "supernode/requests/disqualificator.h"
#include "rta/fullsupernodelist.h"
#include "rta/supernode.h"
#include "lib/graft/binary_serialize.h"

#include <misc_log_ex.h>
#include <boost/shared_ptr.hpp>

#include <wallet/wallet2.h>

#define tst false

#undef MONERO_DEFAULT_LOG_CATEGORY
#define MONERO_DEFAULT_LOG_CATEGORY "supernode.blockchainbasedlistrequest"

namespace
{

static const char* PATH = "/blockchain_based_list";

GRAFT_DEFINE_IO_STRUCT(PingMessage,
                       (uint64_t, block_height),
                       (crypto::hash, block_hash),
                       (crypto::public_key, id)
                       );

GRAFT_DEFINE_IO_STRUCT(SignedPingMessage,
                       (PingMessage, pm),
                       (crypto::signature, self_sign)
                       );

GRAFT_DEFINE_IO_STRUCT(DisqualificationItem,
                       (uint64_t, block_height),
                       (crypto::hash, block_hash),
                       (crypto::public_key, id)
                       );

GRAFT_DEFINE_IO_STRUCT(DisqualificationVotes,
                       (uint64_t, block_height),
                       (crypto::hash, block_hash),
                       (crypto::public_key, signer_id),
                       (std::vector<crypto::public_key>, ids),
                       (std::vector<crypto::signature>, signs) //signs of DisqualificationItem with correcponding ids
                       );

GRAFT_DEFINE_IO_STRUCT(SignerItem,
                       (crypto::public_key, signer_id),
                       (crypto::signature, sign)
                       );

GRAFT_DEFINE_IO_STRUCT(Disqualification,
                       (DisqualificationItem, item),
                       (std::vector<SignerItem>, signers)
                       );

} //namespace

namespace graft::supernode::request {

namespace {

class BBLDisqualificator : public BBLDisqualificatorBase
{
    static constexpr int32_t DESIRED_BBQS_SIZE = graft::FullSupernodeList::DISQUALIFICATION_SAMPLE_SIZE;
    static constexpr int32_t DESIRED_QCL_SIZE = graft::FullSupernodeList::DISQUALIFICATION_CANDIDATES_SIZE;
    static constexpr int32_t REQUIRED_BBQS_VOTES = (DESIRED_BBQS_SIZE*2 + (3-1))/3;
    static constexpr size_t BLOCKCHAIN_BASED_LIST_DELAY_BLOCK_COUNT = graft::FullSupernodeList::BLOCKCHAIN_BASED_LIST_DELAY_BLOCK_COUNT;
    static constexpr size_t DISQUALIFICATION_DURATION_BLOCK_COUNT = 10;

    enum Phases : int
    {
        phase_1,
        phase_2,
        phase_3,
        phase_4,
        phases_count
    };

    static std::mutex m_mutex;

    bool m_started = false;

    uint64_t m_block_height = 0;
    crypto::hash m_block_hash;
    //Blockchain Based Qualification Sample, exclude itself
    std::vector<crypto::public_key> m_bbqs_ids;
    //Qualification Candidate List, exclude itself
    std::vector<crypto::public_key> m_qcl_ids;

    std::vector<crypto::public_key> m_answered_ids;

    crypto::public_key m_supernode_id;
    crypto::secret_key m_secret_key;

    std::vector<std::string> m_bbqs_str_ids;
    std::string m_supernode_str_id;


    bool m_in_bbqs = false;
    bool m_in_qcl = false;
    bool m_collectPings = false;
    bool m_collectVotes = false;

    using DisqId = crypto::public_key;
    using SignerId = crypto::public_key;
    using Sign = crypto::signature;

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

    std::map<DisqId, std::vector<std::pair<SignerId, Sign>>, less_mem<DisqId> > m_votes;

///////////////////// tools return error message if any

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

    template<typename T>
    static void json_serialize(const T& t, std::string& str)
    {
        graft::Output out;
        out.load(t);
        str = out.body;
    }

    template<typename T>
    static bool json_deserialize(const std::string& str, T& t)
    {
        graft::Input in;
        in.body = str;
        return in.get(t);
    }

    void sign(const std::string& str, crypto::signature& sig)
    {
        crypto::hash hash;
        crypto::cn_fast_hash(str.data(), str.size(), hash);
        crypto::generate_signature(hash, m_supernode_id, m_secret_key, sig);
    }

    bool check_sign(const std::string& str, const crypto::public_key id, const crypto::signature& sig)
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

    bool getBBQSandQCL(graft::Context& ctx, uint64_t& block_height, crypto::hash& block_hash, std::vector<crypto::public_key>& bbqs, std::vector<crypto::public_key>& qcl)
    {
        if(fnGetBBQSandQCL)
        {
            uint64_t tmp_block_number = block_height + BLOCKCHAIN_BASED_LIST_DELAY_BLOCK_COUNT;
            fnGetBBQSandQCL(tmp_block_number, block_hash, bbqs, qcl);
            assert(tmp_block_number == block_height);
            block_height = tmp_block_number;
            return true;
        }
        graft::FullSupernodeList::supernode_array suBBQS, suQCL;
        bool res = ctx.global.apply<boost::shared_ptr<graft::FullSupernodeList>>("fsl",
            [&](boost::shared_ptr<graft::FullSupernodeList>& fsl)->bool
        {
            if(!fsl) return false;
            uint64_t tmp_block_number;
            bool res = fsl->buildDisqualificationSamples(block_height+BLOCKCHAIN_BASED_LIST_DELAY_BLOCK_COUNT, suBBQS, suQCL, tmp_block_number);
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

        bbqs.clear();
        bbqs.reserve(suBBQS.size());
        for(auto& item : suBBQS)
        {
            bbqs.push_back(item->idKey());
        }

        qcl.clear();
        qcl.reserve(suQCL.size());
        for(auto& item : suQCL)
        {
            qcl.push_back(item->idKey());
        }
    }

///////////////////// phases
protected:
    graft::Status do_phase1(graft::Context& ctx, uint64_t block_height)
    {
        m_started = true;
        m_block_height = block_height;

        {//get m_supernode_id & m_supernode_str_id & m_secret_key
            getSupenodeKeys(ctx, m_supernode_id, m_secret_key);
            m_supernode_str_id = epee::string_tools::pod_to_hex(m_supernode_id);
        }

        {//generate BBQS & QCL
            m_block_height = block_height;
            bool res = getBBQSandQCL(ctx, m_block_height, m_block_hash, m_bbqs_ids, m_qcl_ids);
            if(!res) return graft::Status::Error;
        }

        std::sort(m_bbqs_ids.begin(), m_bbqs_ids.end(), less_mem<crypto::public_key>{});
        std::sort(m_qcl_ids.begin(), m_qcl_ids.end(), less_mem<crypto::public_key>{});

        {//set m_in_bbqs
            auto pair = std::equal_range(m_bbqs_ids.begin(), m_bbqs_ids.end(), m_supernode_id, less_mem<crypto::public_key>{});
            assert(pair.first == pair.second || std::distance(pair.first, pair.second) == 1 );
            m_in_bbqs = (pair.first != pair.second);
            if(m_in_bbqs)
            {
                m_bbqs_ids.erase(pair.first, pair.second);
            }
        }
        {//set m_in_qcl
            auto pair = std::equal_range(m_qcl_ids.begin(), m_qcl_ids.end(), m_supernode_id, less_mem<crypto::public_key>{});
            assert(pair.first == pair.second || std::distance(pair.first, pair.second) == 1 );
            m_in_qcl = (pair.first != pair.second);
            if(m_in_qcl)
            {
                m_qcl_ids.erase(pair.first, pair.second);
            }
        }

        //make m_bbqs_str_ids
        m_bbqs_str_ids.reserve(m_bbqs_ids.size());
        for(auto& id : m_bbqs_ids)
        {
            m_bbqs_str_ids.push_back( epee::string_tools::pod_to_hex(id) );
        }

        m_answered_ids.clear();
        m_collectPings = m_in_bbqs;

        return graft::Status::Ok;
    }

    graft::Status do_phase2(graft::Context& ctx, graft::Output& output)
    {
        if(!m_started) return graft::Status::Ok;
        m_collectVotes = m_in_bbqs;
        if(m_collectVotes) m_votes.clear();
        if(!m_in_qcl) return graft::Status::Ok;

        std::string spm_str;
        {//prepare bin serialized SignedPingMessage
            SignedPingMessage spm;
            spm.pm.block_height = m_block_height;
            spm.pm.block_hash = m_block_hash;
            spm.pm.id = m_supernode_id;

            //sign
            std::string pm_str;
            bin_serialize(spm.pm, pm_str);
            sign(pm_str, spm.self_sign);

            //serialize
            bin_serialize(spm, spm_str);
        }
        //encrypt
        std::string message;
        graft::crypto_tools::encryptMessage(spm_str, m_bbqs_ids, message);

        //multicast to m_bbqs_ids
        MulticastRequestJsonRpc req;
        req.params.receiver_addresses = m_bbqs_str_ids;
        req.method = "multicast";
        req.params.callback_uri = ROUTE_PING_RESULT; //"/cryptonode/ping_result";
        req.params.data = graft::utils::base64_encode(message);
        req.params.sender_address = m_supernode_str_id;

        output.load(req);
        output.path = "/json_rpc/rta";
        MDEBUG("multicasting: " << output.data());
        MDEBUG(__FUNCTION__ << " end");
        return graft::Status::Forward;
    }

    graft::Status handle_phase2(const graft::Router::vars_t& vars, const graft::Input& input, graft::Context& ctx, graft::Output& output)
    {
        if(!m_started || !m_collectPings) return graft::Status::Ok;
        if(!m_in_bbqs)
        {
            return setError(ctx, "unexpected call of handle_phase2, not in BBQS");
        }

        MulticastRequestJsonRpc req;
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

        SignedPingMessage spm;
        std::string err = bin_deserialize(plain, spm);
        if(!err.empty())
        {
            std::ostringstream oss;
            oss << "cannot deserialize SignedPingMessage, error: '" << err << "'";
            return setError(ctx, oss.str());
        }

        //check reliability
        if(spm.pm.block_height != m_block_height)
        {
            std::ostringstream oss;
            oss << "invalid block_height " << spm.pm.block_height << " expected " << m_block_height;
            return setError(ctx, oss.str(), 201);
        }
        if(spm.pm.block_hash != m_block_hash)
        {
            std::ostringstream oss;
            oss << "invalid block block_hash " << spm.pm.block_hash << " expected " << m_block_hash;
            return setError(ctx, oss.str());
        }
        if(std::none_of(m_qcl_ids.begin(), m_qcl_ids.end(), [&spm](auto& it){ return it == spm.pm.id; } ))
        {
            std::ostringstream oss; oss << "the id '" << spm.pm.id << "' is not in QCL";
            return setError(ctx, oss.str());
        }

        {//check sign of spm
            std::string pm_str;
            bin_serialize(spm.pm, pm_str);
            bool res = check_sign(pm_str, spm.pm.id, spm.self_sign);
            if(!res)
            {
                std::ostringstream oss;
                oss << "invalid self signature '" << spm.self_sign << "' of id '" << spm.pm.id << "'";
                return setError(ctx, oss.str());
            }

        }

        //save SN id
        m_answered_ids.push_back(spm.pm.id);

        return graft::Status::Ok;
    }

    graft::Status do_phase3(graft::Context& ctx, graft::Output& output)
    {
        if(!m_started || !m_in_bbqs) return graft::Status::Ok;
        m_collectPings = false;

        //find difference (m_qcl_ids) - (m_answered_ids)
        std::sort(m_qcl_ids.begin(), m_qcl_ids.end(), less_mem<crypto::public_key>{});
        m_qcl_ids.erase( std::unique( m_qcl_ids.begin(), m_qcl_ids.end() ), m_qcl_ids.end() );

        std::sort(m_answered_ids.begin(), m_answered_ids.end(), less_mem<crypto::public_key>{});
        m_answered_ids.erase( std::unique( m_answered_ids.begin(), m_answered_ids.end() ), m_answered_ids.end() );

        std::vector<crypto::public_key> diff;
        std::set_difference(m_qcl_ids.begin(), m_qcl_ids.end(), m_answered_ids.begin(), m_answered_ids.end(), std::back_inserter(diff), less_mem<crypto::public_key>{} );
        LOG_PRINT_L1("non-answered qcl size: ") << diff.size();
        if(diff.empty()) return graft::Status::Ok;


        //sign each id in diff and push into dv
        DisqualificationVotes dv;
        {
            dv.block_height = m_block_height;
            dv.block_hash = m_block_hash;
            dv.signer_id = m_supernode_id;
            dv.ids.reserve(diff.size());
            dv.signs.reserve(diff.size());

            DisqualificationItem di;
            di.block_height = m_block_height;
            di.block_hash = m_block_hash;
            for(const auto& it : diff)
            {
                di.id = it;
                std::string di_str;
                bin_serialize(di, di_str);
                crypto::signature sig;
                sign(di_str, sig);
                dv.ids.push_back(it);
                dv.signs.push_back(std::move(sig));
            }
        }

        //bin serialize dv
        std::string dv_str;
        bin_serialize(dv, dv_str);
        //encrypt
        std::string message;
        graft::crypto_tools::encryptMessage(dv_str, m_bbqs_ids, message);

        //add my votes to me
        for(size_t i = 0; i < dv.ids.size(); ++i)
        {
            const auto& id = dv.ids[i];
            const auto& sign = dv.signs[i];
            auto& vec = m_votes[id];
            vec.emplace_back( std::make_pair(dv.signer_id, sign) );
        }

        //multicast to m_bbqs_ids
        MulticastRequestJsonRpc req;
        req.params.receiver_addresses = m_bbqs_str_ids;
        req.method = "multicast";
        req.params.callback_uri = ROUTE_VOTES;
        req.params.data = graft::utils::base64_encode(message);
        req.params.sender_address = m_supernode_str_id;

        output.load(req);
        output.path = "/json_rpc/rta";
        MDEBUG("multicasting: " << output.data());
        MDEBUG(__FUNCTION__ << " end");
        return graft::Status::Forward;
    }

    graft::Status handle_phase3(const graft::Router::vars_t& vars, const graft::Input& input, graft::Context& ctx, graft::Output& output)
    {
        if(!m_started || !m_collectVotes) return graft::Status::Ok;
        if(!m_in_bbqs)
        {
            return setError(ctx, "unexpected call of handle_phase3, not in BBQS");
        }

        MulticastRequestJsonRpc req;
        bool res = input.get(req);
        if(!res)
        {
            return setError(ctx, "cannot deserialize MulticastRequestJsonRpc");
        }

        std::string message = graft::utils::base64_decode(req.params.data);
        std::string dv_str;
        bool res2 = graft::crypto_tools::decryptMessage(message, m_secret_key, dv_str);
        if(!res2)
        {
            return setError(ctx, "cannot decrypt, the message is not for me");
        }

        DisqualificationVotes dv;
        std::string err = bin_deserialize(dv_str, dv);
        if(!err.empty())
        {
            std::ostringstream oss;
            oss << "cannot deserialize DisqualificationVotes, error: '" << err << "'";
            return setError(ctx, oss.str());
        }

        //check reliability
        if(dv.block_height != m_block_height)
        {
            std::ostringstream oss;
            oss << "invalid block_height " << dv.block_height << " expected " << m_block_height;
            return setError(ctx, oss.str(), 301);
        }
        if(dv.block_hash != m_block_hash)
        {
            std::ostringstream oss;
            oss << " invalid block_hash " << dv.block_hash << " expected " << m_block_hash;
            return setError(ctx, oss.str());
        }
        if(dv.ids.empty() || dv.signs.size() != dv.ids.size())
        {
            return setError(ctx, "corrupted DisqualificationVotes");
        }
            //assumed m_qcl_ids and m_bbqs_ids are sorted
        //check dv.signer_id in BBQS
        if(!std::binary_search(m_bbqs_ids.cbegin(), m_bbqs_ids.cend(), dv.signer_id, less_mem<crypto::public_key>{} ))
        {
            std::ostringstream oss; oss << "in DisqualificationVotes signer with id '" << dv.signer_id << "'  is not in BBQS ";
            return setError(ctx, oss.str());
        }
        {//check that all dv.ids in QCL  difference (dv.ids) - (m_qcl_ids) is empty
            std::vector<crypto::public_key> diff;
            std::set_difference(dv.ids.begin(), dv.ids.end(), m_qcl_ids.begin(), m_qcl_ids.end(), std::back_inserter(diff), less_mem<crypto::public_key>{});
            if(!diff.empty())
            {
                return setError(ctx, "in DisqualificationVotes some ids is not in QCL");
            }
        }
        {//check signes
            DisqualificationItem di;
            di.block_height = m_block_height;
            di.block_hash = m_block_hash;
            for(size_t i = 0; i < dv.ids.size(); ++i)
            {
                const auto& id = dv.ids[i];
                const auto& sign = dv.signs[i];
                di.id = id;
                std::string di_str;
                bin_serialize(di, di_str);
                bool res = check_sign(di_str, dv.signer_id, sign);
                if(!res)
                {
                    return setError(ctx, "in DisqualificationVotes at least one sign is invalid");
                }
            }
        }

        //save votes
        for(size_t i = 0; i < dv.ids.size(); ++i)
        {
            const auto& id = dv.ids[i];
            const auto& sign = dv.signs[i];
            auto& vec = m_votes[id];
            vec.emplace_back( std::make_pair(dv.signer_id, sign) );
        }

        return graft::Status::Ok;
    }

    graft::Status do_phase4(graft::Context& ctx, graft::Output& output)
    {
        if(!m_started || !m_in_bbqs) return graft::Status::Ok;
        m_collectVotes = false;

        //remove entries from m_votes with not enough votes
        for(auto it = m_votes.begin(), eit = m_votes.end(); it != eit;)
        {
            if(it->second.size() < REQUIRED_BBQS_VOTES)
            {
                MWARNING("Not enough votes to disqualify '") << epee::string_tools::pod_to_hex(it->first)
                    << "' got " << it->second.size() << " required " << REQUIRED_BBQS_VOTES;
                it = m_votes.erase(it);
            }
            else ++it;
        }
        if(m_votes.empty()) return graft::Status::Ok;

        //create disqualifications
        std::vector<Disqualification> ds;
        ds.reserve(m_votes.size());
        for(auto& [id, vec] : m_votes)
        {
            Disqualification d;
            d.item.block_height = m_block_height;
            d.item.block_hash = m_block_hash;
            d.item.id = id;
            d.signers.reserve(vec.size());
            for(auto& [siner_id, sign] : vec)
            {
                SignerItem si;
                si.signer_id = siner_id;
                si.sign = sign;
                d.signers.push_back(std::move(si) );
            }
            ds.push_back(std::move(d));
        }

        //create extras from disqualifications
        using extra2_t = std::vector<uint8_t>;
        std::vector<extra2_t> extra2s;
        extra2s.reserve(ds.size());
        for(auto& d : ds)
        {
            extra2_t ext;
            ext.push_back(TX_EXTRA_GRAFT_DISQUALIFICATION_TAG);
            std::string d_str;
            bin_serialize(d, d_str);
            static_assert(sizeof(d_str[0]) == sizeof(uint8_t));
            std::copy(d_str.begin(), d_str.end(), std::back_inserter(ext));
            extra2s.push_back(std::move(ext));
        }

        //create transactions from extras
        uint32_t unlock_time = m_block_height + DISQUALIFICATION_DURATION_BLOCK_COUNT;
        std::vector<tools::wallet2::pending_tx> txes;
        txes.reserve(extra2s.size());
        for(auto& extra2 : extra2s)
        {
            tools::wallet2::pending_tx ptx;
            cryptonote::transaction& tx = ptx.tx;
            tx.version = 123;
            tx.extra = extra2;
            tx.extra2 = extra2;

            crypto::public_key tmp;
            crypto::generate_keys(tmp, ptx.tx_key);

            ptx.construction_data.extra = tx.extra;
            ptx.construction_data.unlock_time = unlock_time;
            ptx.construction_data.use_rct = false;

            txes.push_back(std::move(ptx));
        }

        if(fnCollectTxs)
        {
            fnCollectTxs(&txes);
            return graft::Status::Ok;
        }

        //create wallet
        std::string addr = ctx.global["cryptonode_rpc_address"];
        bool testnet = ctx.global["testnet"];
        tools::wallet2 wallet(testnet);
        wallet.init(addr);
        wallet.set_refresh_from_block_height(m_block_height);
        wallet.set_seed_language("English");

        try
        {
            wallet.commit_tx(txes);
        }
        catch(std::exception& ex)
        {
            return setError(ctx, ex.what());
        }

        return graft::Status::Ok;
    }

#if(tst)
    graft::Status handle_test(const graft::Router::vars_t& vars, const graft::Input& input, graft::Context& ctx, graft::Output& output)
    {
        if(!m_started) return graft::Status::Error;

        //
        std::vector<Disqualification> ds;
        {
            Disqualification d;
            d.item.block_height = m_block_height;
            d.item.block_hash = m_block_hash;
            d.item.id = m_supernode_id; //self disqualification
            {
                SignerItem si;
                si.signer_id = m_supernode_id;
                //sign
                std::string item_str;
                bin_serialize(d.item, item_str);
                sign(item_str, si.sign);

                d.signers.push_back(std::move(si) );
            }
            ds.push_back(std::move(d));
        }

        //create extras from disqualifications
        using extra2_t = std::vector<uint8_t>;
        std::vector<extra2_t> extra2s;
        extra2s.reserve(ds.size());
        for(auto& d : ds)
        {
            extra2_t ext;
            ext.push_back(TX_EXTRA_GRAFT_DISQUALIFICATION_TAG);
            std::string d_str;
            bin_serialize(d, d_str);
            static_assert(sizeof(d_str[0]) == sizeof(uint8_t));
            std::copy(d_str.begin(), d_str.end(), std::back_inserter(ext));
            extra2s.push_back(std::move(ext));
        }

        //create transactions from extras
        uint32_t unlock_time = m_block_height + DISQUALIFICATION_DURATION_BLOCK_COUNT;
        std::vector<tools::wallet2::pending_tx> txes;
        txes.reserve(extra2s.size());
        for(auto& extra2 : extra2s)
        {
            tools::wallet2::pending_tx ptx;
            cryptonote::transaction& tx = ptx.tx;
            tx.version = 123;
            tx.extra = extra2;
            tx.extra2 = extra2;

            crypto::public_key tmp;
            crypto::generate_keys(tmp, ptx.tx_key);

            ptx.construction_data.extra = tx.extra;
            ptx.construction_data.unlock_time = unlock_time;
            ptx.construction_data.use_rct = false;

            txes.push_back(std::move(ptx));
        }

        //create wallet
        std::string addr = ctx.global["cryptonode_rpc_address"];
        bool testnet = ctx.global["testnet"];
        tools::wallet2 wallet(testnet);
        bool ok = wallet.init(addr);
        if(!ok)
        {
            return setError(ctx, "cannot create temporal wallet");
        }
        wallet.set_refresh_from_block_height(m_block_height);
        wallet.set_seed_language("English");

        try
        {
            wallet.commit_tx(txes);
        }
        catch(std::exception& ex)
        {
            return setError(ctx, ex.what());
        }

        return graft::Status::Ok;
    }
#endif

    graft::Status do_process(const graft::Router::vars_t& vars, const graft::Input& input, graft::Context& ctx, graft::Output& output)
    {
        if(ctx.local.getLastStatus() == graft::Status::Forward) return graft::Status::Ok;
        assert(ctx.global.hasKey("fsl"));

        BlockchainBasedListJsonRpcRequest req;
        if (!input.get(req))
        {
            // can't parse request
            LOG_ERROR("Failed to parse request");
            return Status::Error;
        }

        uint64_t res_block_height = req.params.block_height - BLOCKCHAIN_BASED_LIST_DELAY_BLOCK_COUNT;

        if(res_block_height == m_block_height) return graft::Status::Ok;
        if(res_block_height < m_block_height)
        {
            MDEBUG("Old block_height ") << res_block_height << " current " << m_block_height;
            return graft::Status::Error;
        }

        int phase = req.params.block_height % phases_count;

        switch(phase)
        {
        case phase_1: return do_phase1(ctx, res_block_height);
        case phase_2: return do_phase2(ctx, output);
        case phase_3: return do_phase3(ctx, output);
        case phase_4: return do_phase4(ctx, output);
        default: assert(false);
        }

        return graft::Status::Error;
    }
public:
    static graft::Status process(const graft::Router::vars_t& vars, const graft::Input& input, graft::Context& ctx, graft::Output& output)
    {
        std::lock_guard<std::mutex> lk(m_mutex);

        if(!ctx.global.hasKey("BBLDisqualificator"))
        {
            std::shared_ptr<BBLDisqualificator> bbld = std::make_shared<BBLDisqualificator>();
            ctx.global["BBLDisqualificator"] = bbld;
        }
        std::shared_ptr<BBLDisqualificator> bbld = ctx.global["BBLDisqualificator"];
        return bbld->do_process(vars, input, ctx, output);
    }

    static graft::Status phase2Handler(const graft::Router::vars_t& vars, const graft::Input& input, graft::Context& ctx, graft::Output& output)
    {
        std::lock_guard<std::mutex> lk(m_mutex);

        if(!ctx.global.hasKey("BBLDisqualificator")) return graft::Status::Ok;
        std::shared_ptr<BBLDisqualificator> bbld = ctx.global["BBLDisqualificator"];
        return bbld->handle_phase2(vars, input, ctx, output);
    }

    static graft::Status phase3Handler(const graft::Router::vars_t& vars, const graft::Input& input, graft::Context& ctx, graft::Output& output)
    {
        std::lock_guard<std::mutex> lk(m_mutex);

        if(!ctx.global.hasKey("BBLDisqualificator")) return graft::Status::Ok;
        std::shared_ptr<BBLDisqualificator> bbld = ctx.global["BBLDisqualificator"];
        return bbld->handle_phase3(vars, input, ctx, output);
    }

#if(tst)
    static graft::Status testHandler(const graft::Router::vars_t& vars, const graft::Input& input, graft::Context& ctx, graft::Output& output)
    {
        std::lock_guard<std::mutex> lk(m_mutex);

        if(!ctx.global.hasKey("BBLDisqualificator")) return graft::Status::Ok;
        std::shared_ptr<BBLDisqualificator> bbld = ctx.global["BBLDisqualificator"];
        return bbld->handle_test(vars, input, ctx, output);
    }
#endif

    static constexpr const char* ROUTE_PING_RESULT = "/cryptonode/ping_result";
    static constexpr const char* ROUTE_VOTES = "/cryptonode/votes";
};

std::mutex BBLDisqualificator::m_mutex;

class BBLDisqualificatorTest : public BBLDisqualificator
{
    graft::GlobalContextMap globalContextMap;
    graft::Context ctx = graft::Context(globalContextMap);
    virtual void process_command(const command& cmd, std::vector<crypto::public_key>& forward, std::string& body, std::string& callback_uri) override
    {
        graft::Status res;
        graft::Output output;
        if(cmd.uri.empty())
        {
            BlockchainBasedListJsonRpcRequest req;
            req.params.block_height = cmd.block_height;
            req.params.block_hash = epee::string_tools::pod_to_hex(cmd.block_hash);
            graft::Output o; o.load(req);
            graft::Input in; in.body = o.body;
            res = do_process({}, in, ctx, output);
        }
        else
        {
            graft::Input in; in.body = cmd.body;
            if(cmd.uri == ROUTE_PING_RESULT)
            {
                res = handle_phase2({}, in, ctx, output);
            }
            else
            {
                res = handle_phase3({}, in, ctx, output);
            }
        }
        if(res == graft::Status::Forward)
        {
            body = output.body;
            MulticastRequestJsonRpc req;
            graft::Input in; in.body = output.body; in.getT(req);
            callback_uri = req.params.callback_uri;
            for(auto& id_str : req.params.receiver_addresses)
            {
                crypto::public_key id; epee::string_tools::hex_to_pod(id_str, id);
                forward.push_back(std::move(id));
            }
        }
    }
public:
    static std::mutex m_err_mutex;
    static std::map<int, int> m_errors;

    BBLDisqualificatorTest()
    {
        ctx.global["fsl"] = nullptr;
    }
};

std::mutex BBLDisqualificatorTest::m_err_mutex;
std::map<int, int> BBLDisqualificatorTest::m_errors;

} //namespace

std::unique_ptr<BBLDisqualificatorBase> BBLDisqualificatorBase::createTestBBLDisqualificator(
    GetSupernodeKeys fnGetSupernodeKeys,
    GetBBQSandQCL fnGetBBQSandQCL,
    CollectTxs fnCollectTxs
)
{
    auto disq = std::make_unique<BBLDisqualificatorTest>();
    disq->fnGetSupernodeKeys = fnGetSupernodeKeys;
    disq->fnGetBBQSandQCL = fnGetBBQSandQCL;
    disq->fnCollectTxs = fnCollectTxs;
    return disq;
}

void BBLDisqualificatorBase::count_errors(const std::string& msg, int code)
{
    std::lock_guard<std::mutex> lk(BBLDisqualificatorTest::m_err_mutex);
    ++BBLDisqualificatorTest::m_errors[code];
}

const std::map<int, int>& BBLDisqualificatorBase::get_errors()
{
    return BBLDisqualificatorTest::m_errors;
}

void BBLDisqualificatorBase::clear_errors()
{
    return BBLDisqualificatorTest::m_errors.clear();
}

} //namespace graft::supernode::request

namespace graft::supernode::request {

namespace
{

Status blockchainBasedListHandler
 (const Router::vars_t& vars,
  const graft::Input& input,
  graft::Context& ctx,
  graft::Output& output)
{
    LOG_PRINT_L1(PATH << " called with payload: " << input.data());

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

    BlockchainBasedListJsonRpcRequest req;

    if (!input.get(req))
    { 
        // can't parse request
        LOG_ERROR("Failed to parse request");
        return Status::Error;
    }

      //handle tiers

    FullSupernodeList::blockchain_based_list bbl(req.params.block_hash);

    for (const BlockchainBasedListTier& tier : req.params.tiers)
    {
        const std::vector<BlockchainBasedListTierEntry>& supernode_descs = tier.supernodes;

        FullSupernodeList::blockchain_based_list_tier supernodes;

        supernodes.reserve(supernode_descs.size());

        for (const BlockchainBasedListTierEntry& supernode_desc : supernode_descs)
        {
            FullSupernodeList::blockchain_based_list_entry entry;

            entry.supernode_public_id      = supernode_desc.supernode_public_id;
            entry.supernode_public_address = supernode_desc.supernode_public_address;
            entry.amount                   = supernode_desc.amount;

            supernodes.emplace_back(std::move(entry));
        }

        bbl.tiers.emplace_back(std::move(supernodes));
    }

    fsl->setBlockchainBasedList(req.params.block_height, FullSupernodeList::blockchain_based_list_ptr(
      new FullSupernodeList::blockchain_based_list(std::move(bbl))));

    BBLDisqualificator::process(vars, input, ctx, output);

    return Status::Ok;
}

}

void registerBlockchainBasedListRequest(graft::Router &router)
{
    Router::Handler3 h3(nullptr, blockchainBasedListHandler, nullptr);

    router.addRoute(PATH, METHOD_POST, h3);

    LOG_PRINT_L0("route " << PATH << " registered");

    router.addRoute(BBLDisqualificator::ROUTE_PING_RESULT, METHOD_POST, {nullptr, BBLDisqualificator::phase2Handler , nullptr});
    router.addRoute(BBLDisqualificator::ROUTE_VOTES, METHOD_POST, {nullptr, BBLDisqualificator::phase3Handler , nullptr});

#if(tst)
    router.addRoute("/disqualTest", METHOD_GET | METHOD_POST, {nullptr, BBLDisqualificator::testHandler , nullptr});
#endif
}

}
