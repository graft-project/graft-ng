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

#define __GRAFTLET__
#include "lib/graft/GraftletRegistry.h"
#include "lib/graft/IGraftlet.h"

#include "WalletAddress.h"
#include "supernode/requestdefines.h"
#include "lib/graft/graft_exception.h"
#include "cryptonote_basic/cryptonote_basic_impl.h"
#include "cryptonote_protocol/blobdatatype.h"
#include "file_io_utils.h"

#include<cassert>

#undef MONERO_DEFAULT_LOG_CATEGORY
#define MONERO_DEFAULT_LOG_CATEGORY "graftlet.WalletAddress"

class WalletAddressGraftlet: public IGraftlet
{
public:
    WalletAddressGraftlet(const char* name) : IGraftlet(name) { }

    graft::Status getWalletAddressHandler(const graft::Router::vars_t& vars, const graft::Input& input, graft::Context& ctx, graft::Output& output);
    std::string prepareWalletKey(const graft::CommonOpts& opts);

    virtual void initOnce(const graft::CommonOpts& opts) override
    {
        m_testnet = opts.testnet;
        m_wallet_public_address = opts.wallet_public_address;
        m_id_key = prepareWalletKey(opts);

        REGISTER_ENDPOINT("/dapi/v2.0/cryptonode/getwalletaddress", METHOD_GET | METHOD_POST, WalletAddressGraftlet, getWalletAddressHandler);
    }
private:
    bool m_testnet;
    std::string m_wallet_public_address;
    std::string m_id_key;
};

GRAFTLET_EXPORTS_BEGIN("wallerAddress", GRAFTLET_MKVER(1,1));
GRAFTLET_PLUGIN(WalletAddressGraftlet, IGraftlet, "wallerAddressGL");
GRAFTLET_EXPORTS_END

GRAFTLET_PLUGIN_DEFAULT_CHECK_FW_VERSION(GRAFTLET_MKVER(0,3))

graft::Status WalletAddressGraftlet::getWalletAddressHandler(const graft::Router::vars_t& vars, const graft::Input& input, graft::Context& ctx, graft::Output& output)
{
    using namespace graft::supernode::request;

    LOG_PRINT_L2(__FUNCTION__);
    assert(ctx.local.getLastStatus() == graft::Status::None);
    if(m_wallet_public_address.empty())
    {
        GetWalletAddressErrorResponse res;
        res.testnet = m_testnet;
        output.load(res);
        return graft::Status::Ok;
    }
    GetWalletAddressResponse res;
    res.testnet = m_testnet;
    res.wallet_public_address = m_wallet_public_address;
    res.id_key = m_id_key;
    output.load(res);
    return graft::Status::Ok;
}

std::string WalletAddressGraftlet::prepareWalletKey(const graft::CommonOpts& opts)
{
    if(opts.wallet_public_address.empty()) return std::string();

    boost::filesystem::path data_path(opts.data_dir);

    cryptonote::account_public_address acc = AUTO_VAL_INIT(acc);
    if(!cryptonote::get_account_address_from_str(acc, opts.testnet, opts.wallet_public_address))
    {
        std::ostringstream oss;
        oss << "invalid wallet-public-address '" << opts.wallet_public_address << "'";
        throw graft::exit_error(oss.str());
    }

    crypto::public_key W;
    boost::filesystem::path wallet_keys_file = data_path / "wallet.keys";
    if (!boost::filesystem::exists(wallet_keys_file))
    {
        LOG_PRINT_L0("file '") << wallet_keys_file << "' not found. Generating the keys";
        crypto::secret_key w;
        crypto::generate_keys(W, w);
        //save secret key
        boost::filesystem::path wallet_keys_file_tmp = wallet_keys_file;
        wallet_keys_file_tmp += ".tmp";
        std::string w_str = epee::string_tools::pod_to_hex(w);
        bool r = epee::file_io_utils::save_string_to_file(wallet_keys_file_tmp.string(), w_str);
        if(!r)
        {
            std::ostringstream oss;
            oss << "Cannot write to file '" << wallet_keys_file_tmp << "'";
            throw graft::exit_error(oss.str());
        }
        boost::system::error_code errcode;
        boost::filesystem::rename(wallet_keys_file_tmp, wallet_keys_file, errcode);
        assert(!errcode);
    }
    else
    {
        LOG_PRINT_L1(" Reading wallet keys file '") << wallet_keys_file << "'";
        std::string w_str;
        bool r = epee::file_io_utils::load_file_to_string(wallet_keys_file.string(), w_str);
        if(!r)
        {
            std::ostringstream oss;
            oss << "Cannot read file '" << wallet_keys_file << "'";
            throw graft::exit_error(oss.str());
        }

        crypto::secret_key w;
        cryptonote::blobdata w_data;
        bool ok = epee::string_tools::parse_hexstr_to_binbuff(w_str, w_data) || w_data.size() != sizeof(crypto::secret_key);
        if(ok)
        {
            w = *reinterpret_cast<const crypto::secret_key*>(w_data.data());
            ok = crypto::secret_key_to_public_key(w,W);
        }
        if(!ok)
        {
            std::ostringstream oss;
            oss << "Corrupted data in the file '" << wallet_keys_file << "'";
            throw graft::exit_error(oss.str());
        }
    }

    return epee::string_tools::pod_to_hex(W);
}

namespace
{

struct Informer
{
    Informer()
    {
        LOG_PRINT_L2("graftlet " << getGraftletName() << " loading");
    }
    ~Informer()
    {
        LOG_PRINT_L2("graftlet " << getGraftletName() << " unloading");
    }
};

Informer informer;

} //namespace


