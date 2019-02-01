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

    virtual void initOnce(const graft::CommonOpts& opts) override
    {
        makeGetWalletAddressResponse(opts);

        REGISTER_ENDPOINT("/dapi/v2.0/cryptonode/getwalletaddress", METHOD_GET | METHOD_POST, WalletAddressGraftlet, getWalletAddressHandler);
    }
private:
    graft::Status getWalletAddressHandler(const graft::Router::vars_t& vars, const graft::Input& input, graft::Context& ctx, graft::Output& output);
    void makeGetWalletAddressResponse(const graft::CommonOpts& opts);
    void checkWalletPublicAddress(const graft::CommonOpts& opts);
    void prepareIdKeys(const graft::CommonOpts& opts, crypto::public_key& W, crypto::secret_key& w);
    bool verifySignature();

    graft::supernode::request::GetWalletAddressResponse m_response;
    graft::supernode::request::GetWalletAddressErrorResponse m_errorResponse;
};

GRAFTLET_EXPORTS_BEGIN("wallerAddress", GRAFTLET_MKVER(1,1));
GRAFTLET_PLUGIN(WalletAddressGraftlet, IGraftlet, "wallerAddressGL");
GRAFTLET_EXPORTS_END

GRAFTLET_PLUGIN_DEFAULT_CHECK_FW_VERSION(GRAFTLET_MKVER(0,3))

namespace
{

template<typename POD>
bool parse_hexstr_to_pod(const std::string& s, POD& pod)
{
    cryptonote::blobdata data;
    bool ok = epee::string_tools::parse_hexstr_to_binbuff(s, data)
            && data.size() == sizeof(pod);
    if(!ok) return false;
    pod = *reinterpret_cast<const POD*>(data.data());
}

} //namespace

graft::Status WalletAddressGraftlet::getWalletAddressHandler(const graft::Router::vars_t& vars, const graft::Input& input, graft::Context& ctx, graft::Output& output)
{
    LOG_PRINT_L2(__FUNCTION__);
    assert(ctx.local.getLastStatus() == graft::Status::None);
    if(m_response.wallet_public_address.empty())
    {
        output.load(m_errorResponse);
        return graft::Status::Ok;
    }
    output.load(m_response);
    return graft::Status::Ok;
}

/*!
 * \brief makeGetWalletAddressResponse - fills and signs m_response
 */
void WalletAddressGraftlet::makeGetWalletAddressResponse(const graft::CommonOpts& opts)
{
    m_errorResponse.testnet = opts.testnet;

    if(opts.wallet_public_address.empty()) return;
    checkWalletPublicAddress(opts);

    crypto::public_key W;
    crypto::secret_key w;
    prepareIdKeys(opts, W, w);

    m_response.testnet = opts.testnet;
    m_response.wallet_public_address = opts.wallet_public_address;
    m_response.id_key = epee::string_tools::pod_to_hex(W);

    crypto::signature sign;
    {//sign
        std::string data = m_response.wallet_public_address + ":" + m_response.id_key;
        crypto::hash hash;
        crypto::cn_fast_hash(data.data(), data.size(), hash);
        crypto::generate_signature(hash, W, w, sign);
    }

    m_response.signature = epee::string_tools::pod_to_hex(sign);

    assert(verifySignature());
}

/*!
 * \brief verifySignature - only for testing here, the code can be used on the other side
 */
bool WalletAddressGraftlet::verifySignature()
{
    crypto::public_key W;
    bool ok = parse_hexstr_to_pod(m_response.id_key, W);
    assert(ok);

    crypto::signature sign;
    bool ok1 = parse_hexstr_to_pod(m_response.signature, sign);
    assert(ok1);

    std::string data = m_response.wallet_public_address + ":" + m_response.id_key;
    crypto::hash hash;
    crypto::cn_fast_hash(data.data(), data.size(), hash);
    return crypto::check_signature(hash, W, sign);
}

/*!
 * \brief checkWalletPublicAddress - checks that opts.wallet_public_address is valid
 * on error throws graft::exit_error exception
 */
void WalletAddressGraftlet::checkWalletPublicAddress(const graft::CommonOpts& opts)
{
    cryptonote::account_public_address acc = AUTO_VAL_INIT(acc);
    if(!cryptonote::get_account_address_from_str(acc, opts.testnet, opts.wallet_public_address))
    {
        std::ostringstream oss;
        oss << "invalid wallet-public-address '" << opts.wallet_public_address << "'";
        throw graft::exit_error(oss.str());
    }
}

/*!
 * \brief prepareIdKeys - gets id keys, generates them if required
 * on errors throws graft::exit_error exception
 */
void WalletAddressGraftlet::prepareIdKeys(const graft::CommonOpts& opts, crypto::public_key& W, crypto::secret_key& w)
{
    boost::filesystem::path data_path(opts.data_dir);

    boost::filesystem::path wallet_keys_file = data_path / "wallet.keys";
    if (!boost::filesystem::exists(wallet_keys_file))
    {
        LOG_PRINT_L0("file '") << wallet_keys_file << "' not found. Generating the keys";
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

        bool ok = parse_hexstr_to_pod(w_str, w);
        if(ok)
        {
            ok = crypto::secret_key_to_public_key(w,W);
        }
        if(!ok)
        {
            std::ostringstream oss;
            oss << "Corrupted data in the file '" << wallet_keys_file << "'";
            throw graft::exit_error(oss.str());
        }
    }
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


