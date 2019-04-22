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

#include <misc_log_ex.h>
#include <boost/shared_ptr.hpp>

#undef MONERO_DEFAULT_LOG_CATEGORY
#define MONERO_DEFAULT_LOG_CATEGORY "supernode.blockchainbasedlistrequest"

namespace {
    static const char* PATH = "/blockchain_based_list";

GRAFT_DEFINE_IO_STRUCT(PingMessage,
                       (std::string, id),
                       (uint64_t, block_height),
                       (std::string, block_hash)
                       );

GRAFT_DEFINE_IO_STRUCT(DisqualificationItem,
                       (std::string, id),
                       (std::string, sign)
                       );

GRAFT_DEFINE_IO_STRUCT(DisqualificationRequest,
                       (std::string, signer_id),
                       (uint64_t, block_height),
                       (std::vector<DisqualificationItem>, items)
                       );

GRAFT_DEFINE_IO_STRUCT(SignedMessage,
                       (std::string, json),
                       (std::string, sign)
                       );
}

namespace graft::supernode::request {

namespace {

class BBLDisqualificator
{
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

    uint64_t m_block_height;
    std::string m_block_hash;
    //Blockchain Based Qualification Sample
    std::vector<std::string> m_bbqs_ids;
    //Qualification Candidate List
    std::vector<std::string> m_qcl_ids;

    std::vector<std::string> m_answered_ids;

    std::string m_supernode_id;

    bool m_in_bbqs = false;
    bool m_in_qcl = false;
    bool m_collectPings = false;
    bool m_collectVotes = false;

    using DisqId = std::string;
    using SignerId = std::string;
    using Sign = std::string;

    std::map<DisqId, std::vector<std::pair<SignerId, Sign>>> m_votes;

///////////////////// tools return error message if any

    template<typename T>
    static void serialize(const T& t, std::string& json)
    {
        graft::Output out;
        out.load(t);
        json = out.body;
    }

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

///////////////////// phases

    graft::Status do_phase1(graft::Context& ctx, uint64_t block_height, const std::string& block_hash)
    {
        m_block_height = block_height;
        m_block_hash = block_hash;
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
            for(auto& item : suBBQS)
            {
                m_bbqs_ids.push_back(item->idKeyAsString());
            }

            m_qcl_ids.clear();
            m_qcl_ids.reserve(suQCL.size());
            for(auto& item : suQCL)
            {
                m_qcl_ids.push_back(item->idKeyAsString());
            }
        }

        {//get m_supernode_id
            ctx.global.apply<graft::SupernodePtr>("supernode",
                [this](graft::SupernodePtr& supernode)->bool
            {
                assert(supernode);
                m_supernode_id = supernode->idKeyAsString();
                return true;
            });
        }

        //check if we are in BBQS & QCL
        m_in_bbqs = std::any_of(m_bbqs_ids.begin(), m_bbqs_ids.end(), [this](auto& it)->bool { return it == m_supernode_id; });
        m_in_qcl = std::any_of(m_qcl_ids.begin(), m_qcl_ids.end(), [this](auto& it)->bool { return it == m_supernode_id; });

        m_answered_ids.clear();
        m_collectPings = m_in_bbqs;

        return graft::Status::Ok;
    }

    graft::Status do_phase2(graft::Context& ctx, graft::Output& output)
    {
        if(!m_in_qcl) return graft::Status::Ok;
        m_collectVotes = m_in_bbqs;
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
        //prepare signed message
        {
            PingMessage pm;
            pm.id = m_supernode_id;
            pm.block_height = m_block_height;
            pm.block_hash = m_block_hash;
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
        }
        //encrypt
        std::string message;
//        graft::crypto_tools::encryptMessage(plain, Bkeys, message);
        encryptFor(ctx, plain, m_bbqs_ids, message);

        //multicast to m_bbqs_ids
        MulticastRequestJsonRpc req;
        req.params.receiver_addresses = m_bbqs_ids;
        req.method = "multicast";
        req.params.callback_uri = ROUTE_PING_RESULT; //"/cryptonode/ping_result";
        req.params.data = message;
        req.params.sender_address = m_supernode_id;

        output.load(req);
        output.path = "/json_rpc/rta";
        MDEBUG("multicasting: " << output.data());
        MDEBUG(__FUNCTION__ << " end");
        return graft::Status::Forward;
    }

    graft::Status handle_phase2(const graft::Router::vars_t& vars, const graft::Input& input, graft::Context& ctx, graft::Output& output)
    {
        if(!m_collectPings) return graft::Status::Ok;
//        if(!m_in_bbqs) return graft::Status::Ok;
        assert(m_in_bbqs);

        auto setError = [&ctx](const std::string& msg)
        {
            LOG_ERROR(msg);
            ctx.local.setError(msg.c_str(), graft::Status::Error);
            return graft::Status::Error;
        };

        MulticastRequestJsonRpc req;
        bool res = input.get(req);
        if(!res)
        {
            return setError("cannot deserialize MulticastRequestJsonRpc");
        }

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
                return setError("cannot decrypt, the message is not for me");
            }
        }

        SignedMessage spm;
        {
            graft::Input in; in.body = plain;
            bool res = in.get(spm);
            if(!res)
            {
                return setError("cannot deserialize SignedMessage");
            }
        }
        PingMessage pm;
        {
            graft::Input in; in.body = spm.json;
            bool res = in.get(pm);
            if(!res)
            {
                return setError("cannot deserialize PingMessage");
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
                return setError(err);
            }
        }

        //check reliability
        if(pm.block_height != m_block_height)
        {
            std::ostringstream oss; oss << "invalid block_height " << pm.block_height << " expected " << m_block_height;
            return setError(oss.str());
        }
        if(!std::any_of(m_qcl_ids.begin(), m_qcl_ids.end(), [&pm](auto& it){ return it == pm.id; } ))
        {
            std::ostringstream oss; oss << "the id '" << pm.id << "' is not in QCL";
            return setError(oss.str());
        }

        //save SN id
        m_answered_ids.push_back(pm.id);

        return graft::Status::Ok;
    }

    graft::Status do_phase3(graft::Context& ctx, graft::Output& output)
    {
        if(!m_in_bbqs) return graft::Status::Ok;
        m_collectPings = false;


        //find difference (m_qcl_ids) - (m_answered_ids)
        std::sort(m_qcl_ids.begin(), m_qcl_ids.end());
        m_qcl_ids.erase( std::unique( m_qcl_ids.begin(), m_qcl_ids.end() ), m_qcl_ids.end() );

        std::sort(m_answered_ids.begin(), m_answered_ids.end());
        m_answered_ids.erase( std::unique( m_answered_ids.begin(), m_answered_ids.end() ), m_answered_ids.end() );

        std::vector<std::string> diff;
        std::set_difference(m_qcl_ids.begin(), m_qcl_ids.end(), m_answered_ids.begin(), m_answered_ids.end(), std::back_inserter(diff));
        LOG_PRINT_L1("non-answered qcl size: ") << diff.size();
        if(diff.empty()) return graft::Status::Ok;

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

        //multicast to m_bbqs_ids
        MulticastRequestJsonRpc req;
        req.params.receiver_addresses = m_bbqs_ids;
        req.method = "multicast";
        req.params.callback_uri = ROUTE_VOTES;
        req.params.data = message;
        req.params.sender_address = m_supernode_id;

        output.load(req);
        output.path = "/json_rpc/rta";
        MDEBUG("multicasting: " << output.data());
        MDEBUG(__FUNCTION__ << " end");
        return graft::Status::Forward;
    }

    graft::Status handle_phase3(const graft::Router::vars_t& vars, const graft::Input& input, graft::Context& ctx, graft::Output& output)
    {
        if(!m_collectVotes) return graft::Status::Ok;
        assert(m_in_bbqs);

        auto setError = [&ctx](const std::string& msg)
        {
            LOG_ERROR(msg);
            ctx.local.setError(msg.c_str(), graft::Status::Error);
            return graft::Status::Error;
        };

        MulticastRequestJsonRpc req;
        bool res = input.get(req);
        if(!res)
        {
            return setError("cannot deserialize MulticastRequestJsonRpc");
        }

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
                return setError("cannot decrypt, the message is not for me");
            }
        }

        SignedMessage sdr;
        {
            graft::Input in; in.body = plain;
            bool res = in.get(sdr);
            if(!res)
            {
                return setError("cannot deserialize SignedMessage");
            }
        }
        DisqualificationRequest dr;
        {
            graft::Input in; in.body = sdr.json;
            bool res = in.get(dr);
            if(!res)
            {
                return setError("cannot deserialize PingMessage");
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
                return setError(err);
            }
        }

        //check reliability
        if(dr.block_height != m_block_height)
        {
            std::ostringstream oss; oss << "invalid block_height " << dr.block_height << " expected " << m_block_height;
            return setError(oss.str());
        }
        if(!std::any_of(m_bbqs_ids.begin(), m_bbqs_ids.end(), [&dr](auto& it){ return it == dr.signer_id; } ))
        {
            std::ostringstream oss; oss << "the id '" << dr.signer_id << "' is not in BBQS";
            return setError(oss.str());
        }

        /////////////////////////
        //save votes
        for(auto& it : dr.items)
        {
            auto& vec = m_votes[it.id];
            vec.emplace_back( std::make_pair(dr.signer_id, it.sign) );
        }

        return graft::Status::Ok;
    }

    graft::Status do_process(const graft::Router::vars_t& vars, const graft::Input& input, graft::Context& ctx, graft::Output& output)
    {
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
        std::string block_hash;

        switch(phase)
        {
        case phase_1: return do_phase1(ctx, req.params.block_height, block_hash);
        //TODO: do_phase2 can return Forward
        case phase_2: return do_phase2(ctx, output);
        case phase_3: return do_phase3(ctx, output);
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
