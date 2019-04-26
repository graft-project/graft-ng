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
#include "rta/fullsupernodelist.h"
#include "rta/supernode.h"
#include <utils/cryptmsg.h>
#include "supernode/graft_wallet2.h"
#include "lib/graft/binary_serialize.h"

#include <misc_log_ex.h>
#include <boost/shared_ptr.hpp>

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
//                       (std::vector<SignedDisqualificationItem>, items)
                       );

GRAFT_DEFINE_IO_STRUCT(SignerItem,
                       (crypto::public_key, signer_id),
                       (crypto::signature, sign)
                       );

GRAFT_DEFINE_IO_STRUCT(DisqualificationRequest,
                       (DisqualificationItem, item),
                       (std::vector<SignerItem>, siners)
                       );

/*
GRAFT_DEFINE_IO_STRUCT(SignedDisqualificationItem,
                       (DisqualificationItem, item),
                       (crypto::signature, sign)
                       );

GRAFT_DEFINE_IO_STRUCT(SignedMessage,
                       (std::string, json),
                       (std::string, sign)
                       );
*/
} //namespace

namespace graft::supernode::request {

namespace {

class BBLDisqualificator
{
    static constexpr int32_t DESIRED_BBQS_SIZE = graft::FullSupernodeList::DISQUALIFICATION_SAMPLE_SIZE;
    static constexpr int32_t DESIRED_QCL_SIZE = graft::FullSupernodeList::DISQUALIFICATION_CANDIDATES_SIZE;
    static constexpr int32_t REQUIRED_BBQS_VOTES = (DESIRED_BBQS_SIZE*2 + (3-1))/3;

    enum Phases : int
    {
        phase_1,
        phase_2,
        phase_3,
        phase_4,
        phases_count
    };

//    static constexpr int32 DESIRED_BBQS_SIZE = 8;
//    static constexpr int32 DESIRED_QCL_SIZE = 8;
    bool m_started = false;

    uint64_t m_block_height;
    crypto::hash m_block_hash;
    //Blockchain Based Qualification Sample, exclude itself
//    std::vector<std::string> m_bbqs_ids;
    std::vector<crypto::public_key> m_bbqs_ids;
    //Qualification Candidate List, exclude itself
    std::vector<crypto::public_key> m_qcl_ids;

    std::vector<crypto::public_key> m_answered_ids;

//    std::string m_supernode_id;
    crypto::public_key m_supernode_id;
    crypto::secret_key m_secret_key;

    std::vector<std::string> m_bbqs_str_ids;
    std::string m_supernode_str_id;


    bool m_in_bbqs = false;
    bool m_in_qcl = false;
    bool m_collectPings = false;
    bool m_collectVotes = false;

/*
    using DisqId = std::string;
    using SignerId = std::string;
    using Sign = std::string;
*/
    using DisqId = crypto::public_key;
    using SignerId = crypto::public_key;
    using Sign = crypto::signature;
/*
    template<typename T>
    static bool less_mem(const T& l, const T& r)
    {
        static_assert(std::is_trivially_copyable<T>::value);
        int res = std::memcmp(&l, &r, sizeof(T));
        return (res<0);
    }
*/
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
/*
    static bool less_mem(const T& l, const T& r)
    {
        static_assert(std::is_trivially_copyable<T>::value);
        int res = std::memcmp(&l, &r, sizeof(T));
        return (res<0);
    }
*/
    std::map<DisqId, std::vector<std::pair<SignerId, Sign>>, less_mem<DisqId> > m_votes;

///////////////////// tools return error message if any

    static graft::Status setError(graft::Context& ctx, const std::string& msg)
    {
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
/*
    void makeSignedMessage(graft::Context& ctx, const std::string& json, SignedMessage& sm)
    {
        //sdr.sign = sign(sdr.json)
        ctx.global.apply<graft::SupernodePtr>("supernode",
            [&sm](graft::SupernodePtr& supernode)->bool
        {
            assert(supernode);
            crypto::signature sign;
            bool res = supernode->signMessage(sm.json, sign);
            assert(res);
            sm.sign = epee::string_tools::pod_to_hex(sign);
            return true;
        });
    }
*/
/*
    //encrypts to ids excluding itself
    void encryptFor(graft::Context& ctx, const std::string& plain, const std::vector<std::string>& ids, std::string& message)
    {
        std::vector<crypto::public_key> Bkeys;

        //get Bkeys from ids
        bool res = ctx.global.apply<boost::shared_ptr<graft::FullSupernodeList>>("fsl",
            [&ids, &Bkeys, this](boost::shared_ptr<graft::FullSupernodeList>& fsl)->bool
        {
            if(!fsl) return false;
            Bkeys.reserve(ids.size());
            for(auto& id : ids)
            {
                if(id == m_supernode_id) continue;
                graft::SupernodePtr su = fsl->get(id);
                assert(su);
                Bkeys.emplace_back(su->idKey());
            }
            return true;
        });
        assert(res);

        //encrypt
        graft::crypto_tools::encryptMessage(plain, Bkeys, message);
    }
*/
/*
    std::string decryptForMe(graft::Context& ctx, const std::string& message, std::string& plain)
    {
        crypto::secret_key bkey;
        ctx.global.apply<graft::SupernodePtr>("supernode",
            [&bkey](graft::SupernodePtr& supernode)->bool
        {
            assert(supernode);
            bkey = supernode->secretKey();
            return true;
        });
        bool res = graft::crypto_tools::decryptMessage(message, bkey, plain);
        if(!res)
        {
            return "cannot decrypt, the message is not for me";
        }
        return std::string();
    }
*/
///////////////////// phases

    graft::Status do_phase1(graft::Context& ctx, uint64_t block_height, const std::string& block_hash)
    {
        m_started = true;
        m_block_height = block_height;
//        m_block_hash = block_hash;
        bool res = epee::string_tools::hex_to_pod(block_hash, m_block_hash);
        assert(res);
/*
        //create BBQS & QCL
        auto get_ids = [](graft::Context& ctx, uint64_t block_height, const std::string& seed, int32_t desired_size, std::vector<std::string>& ids) -> bool
        {
            graft::FullSupernodeList::supernode_array supernodes;
            bool res = ctx.global.apply<boost::shared_ptr<graft::FullSupernodeList>>("fsl",
                [&supernodes, block_height, &seed, desired_size](boost::shared_ptr<graft::FullSupernodeList>& fsl)->bool
            {
                if(!fsl) return false;
                uint64_t tmp_block_number;
                bool res = fsl->buildAuthSample(block_height, seed, supernodes, tmp_block_number, desired_size);
                assert(res);
                assert(tmp_block_number == block_height);
                return true;
            });
            if(!res) return false;
            ids.clear();
            ids.reserve(supernodes.size());
            for(auto& item : supernodes)
            {
                ids.push_back(item->idKeyAsString());
            }
            return res;
        };

        get_ids(ctx, block_height, block_hash, DESIRED_BBQS_SIZE, m_bbqs_ids);
        get_ids(ctx, block_height, "", DESIRED_QCL_SIZE, m_qcl_ids); //don't seed
*/
        {//get m_supernode_id & m_
            ctx.global.apply<graft::SupernodePtr>("supernode",
                [this](graft::SupernodePtr& supernode)->bool
            {
                assert(supernode);
//                m_supernode_id = supernode->idKeyAsString();
                m_supernode_id = supernode->idKey();
                m_supernode_str_id = epee::string_tools::pod_to_hex(m_supernode_id);
                m_secret_key = supernode->secretKey();
                return true;
            });
        }

        {//generate BBQS & QCL
            graft::FullSupernodeList::supernode_array suBBQS, suQCL;
            bool res = ctx.global.apply<boost::shared_ptr<graft::FullSupernodeList>>("fsl",
                [&suBBQS, &suQCL, block_height](boost::shared_ptr<graft::FullSupernodeList>& fsl)->bool
            {
                if(!fsl) return false;
                uint64_t tmp_block_number;
                bool res = fsl->buildDisqualificationSamples(block_height, suBBQS, suQCL, tmp_block_number);
                assert(res);
                assert(tmp_block_number == block_height);
                return true;
            });
            if(!res) return graft::Status::Error;

            m_bbqs_ids.clear();
            m_bbqs_ids.reserve(suBBQS.size());
            m_bbqs_str_ids.clear();
            m_bbqs_str_ids.reserve(suBBQS.size());
            for(auto& item : suBBQS)
            {
                auto& pub_key = item->idKey();
                if(m_supernode_id == pub_key)
                {
                    m_in_bbqs = true;
                    continue;
                }
                m_bbqs_ids.push_back(pub_key);
                m_bbqs_str_ids.push_back( epee::string_tools::pod_to_hex(pub_key) );
            }
            std::sort(m_bbqs_ids.begin(), m_bbqs_ids.end(), less_mem<crypto::public_key>{});

            m_qcl_ids.clear();
            m_qcl_ids.reserve(suQCL.size());
            for(auto& item : suQCL)
            {
                auto& pub_key = item->idKey();
                if(m_supernode_id == pub_key)
                {
                    m_in_qcl = true;
                    continue;
                }
                m_qcl_ids.push_back(pub_key);
            }
            std::sort(m_qcl_ids.begin(), m_qcl_ids.end(), less_mem<crypto::public_key>{});
        }

        //check if we are in BBQS & QCL
//        m_in_bbqs = std::any_of(m_bbqs_ids.begin(), m_bbqs_ids.end(), [this](auto& it)->bool { return it == m_supernode_id; });
//        m_in_qcl = std::any_of(m_qcl_ids.begin(), m_qcl_ids.end(), [this](auto& it)->bool { return it == m_supernode_id; });

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
/*
        std::vector<crypto::public_key> Bkeys;

        //get Bkeys from m_bbqs_ids
        bool res = ctx.global.apply<boost::shared_ptr<graft::FullSupernodeList>>("fsl",
            [&Bkeys, this](boost::shared_ptr<graft::FullSupernodeList>& fsl)->bool
        {
            if(!fsl) return false;
            Bkeys.reserve(m_bbqs_ids.size());
            for(auto& id : m_bbqs_ids)
            {
                graft::SupernodePtr su = fsl->get(id);
                assert(su);
                Bkeys.emplace_back(su->idKey());
            }
            return true;
        });
        assert(res);
*/
        std::string plain;
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
            bin_serialize(spm, plain);
/*
            PingMessage pm;
            pm.block_height = m_block_height;
            pm.block_hash = m_block_hash;
            pm.id = m_supernode_id;


            graft::Output out;
            out.load(pm);

            SignedMessage spm;
            spm.json = out.body;

            //spm.sign = sign(spm.json)
            ctx.global.apply<graft::SupernodePtr>("supernode",
                [&spm](graft::SupernodePtr& supernode)->bool
            {
                assert(supernode);
                crypto::signature sign;
                bool res = supernode->signMessage(spm.json, sign);
                assert(res);
                spm.sign = epee::string_tools::pod_to_hex(sign);
                return true;
            });

            out.load(spm);
            plain = out.body;
*/
        }
        //encrypt
        std::string message;
        graft::crypto_tools::encryptMessage(plain, m_bbqs_ids, message);
//        encryptFor(ctx, plain, m_bbqs_ids, message);

        //multicast to m_bbqs_ids
        MulticastRequestJsonRpc req;
//        req.params.receiver_addresses = m_bbqs_ids;
        req.params.receiver_addresses = m_bbqs_str_ids;
        req.method = "multicast";
        req.params.callback_uri = ROUTE_PING_RESULT; //"/cryptonode/ping_result";
        req.params.data = message;
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
//        if(!m_in_bbqs) return graft::Status::Ok;
        assert(m_in_bbqs);
/*
        auto setError = [&ctx](const std::string& msg)
        {
            LOG_ERROR(msg);
            ctx.local.setError(msg.c_str(), graft::Status::Error);
            return graft::Status::Error;
        };
*/
        MulticastRequestJsonRpc req;
        bool res1 = input.get(req);
        if(!res1)
        {
            return setError(ctx, "cannot deserialize MulticastRequestJsonRpc");
        }
/*
        //decrypt
        std::string plain;
        {
            crypto::secret_key bkey;
            ctx.global.apply<graft::SupernodePtr>("supernode",
                [&bkey](graft::SupernodePtr& supernode)->bool
            {
                assert(supernode);
                bkey = supernode->secretKey();
                return true;
            });
            bool res = graft::crypto_tools::decryptMessage(req.params.data, bkey, plain);
            if(!res)
            {
                return setError(ctx, "cannot decrypt, the message is not for me");
            }
        }
*/
        std::string plain;
        bool res2 = graft::crypto_tools::decryptMessage(req.params.data, m_secret_key, plain);
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
        if(spm.pm.block_height != m_block_height || spm.pm.block_hash != m_block_hash)
        {
            std::ostringstream oss;
            oss << "invalid block_height " << spm.pm.block_height << " expected " << m_block_height
                << " or block_hash " << spm.pm.block_hash << " expected " << m_block_hash;
            return setError(ctx, oss.str());
        }
        if(!std::any_of(m_qcl_ids.begin(), m_qcl_ids.end(), [&spm](auto& it){ return it == spm.pm.id; } ))
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
/*
        std::string plain;
        std::string err = decryptForMe(ctx, req.params.data, plain);
        if(!err.empty())
        {
            return setError(ctx, err);
        }

        SignedMessage spm;
        {
            graft::Input in; in.body = plain;
            bool res = in.get(spm);
            if(!res)
            {
                return setError(ctx, "cannot deserialize SignedMessage");
            }
        }
        PingMessage pm;
        {
            graft::Input in; in.body = spm.json;
            bool res = in.get(pm);
            if(!res)
            {
                return setError(ctx, "cannot deserialize PingMessage");
            }
        }

        {//check signature
            std::string err;
            ctx.global.apply<boost::shared_ptr<graft::FullSupernodeList>>("fsl",
                [&err, &pm, &spm, this](boost::shared_ptr<graft::FullSupernodeList>& fsl)->bool
            {
                if(!fsl) return false;
                graft::SupernodePtr su = fsl->get(pm.id);
                if(!su)
                {
                    std::ostringstream oss;
                    oss << "cannot find supernode with id '" << pm.id << "'";
                    err = oss.str();
                    return false;
                }
                crypto::signature sign;
                bool res = epee::string_tools::hex_to_pod(spm.sign, sign);
                if(res) res = su->verifySignature(spm.json, su->idKey(), sign);
                if(!res)
                {
                    std::ostringstream oss;
                    oss << "invalid signature '" << spm.sign << "'";
                    err = oss.str();
                }
                return true;
            });
            if(!err.empty())
            {
                return setError(ctx, err);
            }
        }

        //check reliability
        if(pm.block_height != m_block_height)
        {
            std::ostringstream oss; oss << "invalid block_height " << pm.block_height << " expected " << m_block_height;
            return setError(ctx, oss.str());
        }
        if(!std::any_of(m_qcl_ids.begin(), m_qcl_ids.end(), [&pm](auto& it){ return it == pm.id; } ))
        {
            std::ostringstream oss; oss << "the id '" << pm.id << "' is not in QCL";
            return setError(ctx, oss.str());
        }
*/
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
                dv.signs.push_back(sig);
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
/*
        //sign each id in diff and push into dr
        DisqualificationRequest dr;
        dr.signer_id = m_supernode_id;
        dr.block_height = m_block_height;
        dr.items.reserve(diff.size());
        ctx.global.apply<graft::SupernodePtr>("supernode",
            [&dr, &diff](graft::SupernodePtr& supernode)->bool
        {
            assert(supernode);
            for(const auto& it : diff)
            {
                DisqualificationItem di;
                di.id = it;
                crypto::signature sign;
                bool res = supernode->signMessage(it, sign);
                assert(res);
                di.sign = epee::string_tools::pod_to_hex(sign);
                dr.items.emplace_back(std::move(di));
            }
            return true;
        });

        SignedMessage sdr;
        //sign dr
        {//serialize dr
            graft::Output out;
            out.load(dr);
            sdr.json = out.body;
        }
        //sdr.sign = sign(sdr.json)
        ctx.global.apply<graft::SupernodePtr>("supernode",
            [&sdr](graft::SupernodePtr& supernode)->bool
        {
            assert(supernode);
            crypto::signature sign;
            bool res = supernode->signMessage(sdr.json, sign);
            assert(res);
            sdr.sign = epee::string_tools::pod_to_hex(sign);
            return true;
        });

        std::string plain;
        {//serialize sdr
            graft::Output out;
            out.load(sdr);
            plain = out.body;
        }

        //encrypt
        std::string message;
        encryptFor(ctx, plain, m_bbqs_ids, message);
*/
        //multicast to m_bbqs_ids
        MulticastRequestJsonRpc req;
        req.params.receiver_addresses = m_bbqs_str_ids;
        req.method = "multicast";
        req.params.callback_uri = ROUTE_VOTES;
        req.params.data = message;
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
        assert(m_in_bbqs);
/*
        auto setError = [&ctx](const std::string& msg)
        {
            LOG_ERROR(msg);
            ctx.local.setError(msg.c_str(), graft::Status::Error);
            return graft::Status::Error;
        };
*/
        MulticastRequestJsonRpc req;
        bool res = input.get(req);
        if(!res)
        {
            return setError(ctx, "cannot deserialize MulticastRequestJsonRpc");
        }
/*
        //decrypt
        std::string plain;
        {
            crypto::secret_key bkey;
            ctx.global.apply<graft::SupernodePtr>("supernode",
                [&bkey](graft::SupernodePtr& supernode)->bool
            {
                assert(supernode);
                bkey = supernode->secretKey();
                return true;
            });
            bool res = graft::crypto_tools::decryptMessage(req.params.data, bkey, plain);
            if(!res)
            {
                return setError(ctx, "cannot decrypt, the message is not for me");
            }
        }
*/

        std::string dv_str;
        bool res2 = graft::crypto_tools::decryptMessage(req.params.data, m_secret_key, dv_str);
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
        if(dv.block_height != m_block_height || dv.block_hash != m_block_hash)
        {
            std::ostringstream oss;
            oss << "invalid block_height " << dv.block_height << " expected " << m_block_height
                << " or block_hash " << dv.block_hash << " expected " << m_block_hash;
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
/*

        decltype(m_votes[0].second) vec;
        vec.reserve(dv.ids.size());
        vec.push_back( std::make_pair())




        for(auto i : std::indices())

        m_bbqs_ids
        m_qcl_ids
        std::

        if(!std::any_of(m_qcl_ids.begin(), m_qcl_ids.end(), [&spm](auto& it){ return it == spm.pm.id; } ))
        {
            std::ostringstream oss; oss << "the id '" << spm.pm.id << "' is not in QCL";
            return setError(ctx, oss.str());
        }


        //decrypt
        std::string plain;
        std::string err = decryptForMe(ctx, req.params.data, plain);
        if(!err.empty())
        {
            return setError(ctx, err);
        }

        SignedMessage sdr;
        {
            graft::Input in; in.body = plain;
            bool res = in.get(sdr);
            if(!res)
            {
                return setError(ctx, "cannot deserialize SignedMessage");
            }
        }
        DisqualificationRequest dr;
        {
            graft::Input in; in.body = sdr.json;
            bool res = in.get(dr);
            if(!res)
            {
                return setError(ctx, "cannot deserialize PingMessage");
            }
        }

        {//check signatures
            std::string err;
            ctx.global.apply<boost::shared_ptr<graft::FullSupernodeList>>("fsl",
                [&err, &dr, &sdr, this](boost::shared_ptr<graft::FullSupernodeList>& fsl)->bool
            {
                if(!fsl) return false;
                graft::SupernodePtr su = fsl->get(dr.signer_id);
                if(!su)
                {
                    std::ostringstream oss;
                    oss << "cannot find supernode with id '" << dr.signer_id << "'";
                    err = oss.str();
                    return false;
                }
                crypto::signature sign;
                bool res = epee::string_tools::hex_to_pod(sdr.sign, sign);
                if(res) res = su->verifySignature(sdr.json, su->idKey(), sign);
                if(!res)
                {
                    std::ostringstream oss;
                    oss << "invalid signature '" << sdr.sign << "'";
                    err = oss.str();
                }
                for(const auto& it : dr.items)
                {
                    crypto::signature sign;
                    bool res = epee::string_tools::hex_to_pod(it.sign, sign);
                    if(res) res = su->verifySignature(it.id, su->idKey(), sign);
                    if(!res)
                    {
                        std::ostringstream oss;
                        oss << "invalid signature '" << it.sign << "' of id '" << it.id << "'";
                        err = oss.str();
                    }
                }
                return true;
            });
            if(!err.empty())
            {
                return setError(ctx, err);
            }
        }

        //check reliability
        if(dr.block_height != m_block_height)
        {
            std::ostringstream oss; oss << "invalid block_height " << dr.block_height << " expected " << m_block_height;
            return setError(ctx, oss.str());
        }
        if(!std::any_of(m_bbqs_ids.begin(), m_bbqs_ids.end(), [&dr](auto& it){ return it == dr.signer_id; } ))
        {
            std::ostringstream oss; oss << "the id '" << dr.signer_id << "' is not in BBQS";
            return setError(ctx, oss.str());
        }

        /////////////////////////
        //save votes
        for(auto& it : dr.items)
        {
            auto& vec = m_votes[it.id];
            vec.emplace_back( std::make_pair(dr.signer_id, it.sign) );
        }

        return graft::Status::Ok;
*/
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
                it = m_votes.erase(it);
            }
            else ++it;
        }
        if(m_votes.empty()) return graft::Status::Ok;
        //
        std::vector<DisqualificationRequest> drs;
        drs.reserve(m_votes.size());
        for(auto& [id, vec] : m_votes)
        {
//            auto& [siner_id, sign] = pair;
            DisqualificationRequest dr;
            dr.item.block_height = m_block_height;
            dr.item.block_hash = m_block_hash;
            dr.item.id = id;
            dr.siners.reserve(vec.size());
            for(auto& [siner_id, sign] : vec)
            {
                SignerItem si;
                si.signer_id = siner_id;
                si.sign = sign;
                dr.siners.push_back(std::move(si) );
            }
            drs.push_back(std::move(dr));
        }
//        m_votes.erase( std::remove_if(m_votes.begin(), m_votes.end(), [](auto& it)->bool { return it->second.size() < REQUIRED_BBQS_VOTES; }), m_votes.end());

        //create wallet
        std::string addr = ctx.global["cryptonode_rpc_address"];
        bool testnet = ctx.global["testnet"];
        auto pos = addr.find(':');
        assert(pos != std::string::npos);
        std::string nodeIp = addr.substr(0, pos);
        int nodePort = std::stoi(addr.substr(pos+1));
        std::unique_ptr<tools::GraftWallet2> wal =
                tools::GraftWallet2::createWallet("", nodeIp, nodePort, "", testnet);
        wal->set_refresh_from_block_height(m_block_height);
        if (!wal)
        {
            return setError(ctx, "cannot create temporal wallet");
        }
        wal->set_seed_language("English");
        try
        {
            crypto::secret_key dummy_key;
            wal->generate_graft("", dummy_key, false, false);
        }
        catch (const std::exception& e)
        {
            return setError(ctx, "cannot generate_graft with temporal wallet");
        }


        //TODO: fill this
        uint32_t unlock_time;
        uint32_t priority;
        bool trusted_daemon;
        using extra2_t = std::vector<uint8_t>;
        std::vector<extra2_t> extra2s;
        extra2s.reserve(drs.size());
        for(auto& dr : drs)
        {
            extra2_t ext;
            ext.push_back(TX_EXTRA_GRAFT_DISQUALIFICATION_TAG);
            std::string dr_str;
            bin_serialize(dr, dr_str);
            static_assert(sizeof(dr_str[0]) == sizeof(uint8_t));
            std::copy(dr_str.begin(), dr_str.end(), std::back_inserter(ext));
            extra2s.push_back(std::move(ext));
        }

//        uint64_t upper_transaction_size_limit = get_upper_transaction_size_limit();


//        std::vector<tools::GraftWallet2::pending_tx> txes = wal->create_disqualification_transactions(unlock_time, priority, extra, trusted_daemon);

//        wal->create_transactions_2() //

//        PendingTransaction *GraftWallet2::createTransaction(
/*
        bool simple_wallet::stake_transfer(const std::vector<std::string> &args_)
        {
          return transfer_main(TransferStake, args_);
        }
*/

        cryptonote::tx_extra_graft_rta_signatures tmp;

        return graft::Status::Ok;
    }

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

        int phase = req.params.block_height % phases_count;
        //TODO: get block_hash
//        std::string block_hash = req.params.block_;

        switch(phase)
        {
        case phase_1: return do_phase1(ctx, req.params.block_height, req.params.block_hash);
////        //TODO: do_phase2 can return Forward
        case phase_2: return do_phase2(ctx, output);
        case phase_3: return do_phase3(ctx, output);
        case phase_4: return do_phase4(ctx, output);
        default: assert(false);
        }

//        boost::shared_ptr<graft::FullSupernodeList> fsl = ctx.global["fsl"];
//        fsl->buildAuthSample(block_hash, )

    }
public:
    static graft::Status process(const graft::Router::vars_t& vars, const graft::Input& input, graft::Context& ctx, graft::Output& output)
    {
        if(!ctx.global.hasKey("BBLDisqualificator"))
        {
            std::shared_ptr<BBLDisqualificator> bbld = std::make_shared<BBLDisqualificator>();
            ctx.global["BBLDisqualificator"] = bbld;
        }
        std::shared_ptr<BBLDisqualificator> bbld = ctx.global["BBLDisqualificator"];
        return bbld->process(vars, input, ctx, output);
    }

    static graft::Status phase2Handler(const graft::Router::vars_t& vars, const graft::Input& input, graft::Context& ctx, graft::Output& output)
    {
        if(!ctx.global.hasKey("BBLDisqualificator")) return graft::Status::Ok;
        std::shared_ptr<BBLDisqualificator> bbld = ctx.global["BBLDisqualificator"];
        return bbld->handle_phase2(vars, input, ctx, output);
    }

    static graft::Status phase3Handler(const graft::Router::vars_t& vars, const graft::Input& input, graft::Context& ctx, graft::Output& output)
    {
        if(!ctx.global.hasKey("BBLDisqualificator")) return graft::Status::Ok;
        std::shared_ptr<BBLDisqualificator> bbld = ctx.global["BBLDisqualificator"];
        return bbld->handle_phase3(vars, input, ctx, output);
    }


    static constexpr const char* ROUTE_PING_RESULT = "/cryptonode/ping_result";
    static constexpr const char* ROUTE_VOTES = "/cryptonode/votes";
};

} //namespace
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
}

}
