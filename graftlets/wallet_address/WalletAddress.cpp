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
#include "rta/supernode.h"
#include "lib/graft/graft_exception.h"
#include "cryptonote_basic/cryptonote_basic_impl.h"
#include "cryptonote_protocol/blobdatatype.h"
#include "file_io_utils.h"

#include<cassert>

#undef MONERO_DEFAULT_LOG_CATEGORY
#define MONERO_DEFAULT_LOG_CATEGORY "graftlet.WalletAddress"

class WalletAddress: public IGraftlet
{
public:
    WalletAddress(const char* name) : IGraftlet(name) { }

    virtual void initOnce(const graft::CommonOpts& /*opts*/) override
    {
        REGISTER_ENDPOINT("/dapi/v2.0/cryptonode/getwalletaddress", METHOD_GET | METHOD_POST, WalletAddress, getWalletAddressHandler);
    }
private:
    graft::Status getWalletAddressHandler(const graft::Router::vars_t& vars, const graft::Input& input, graft::Context& ctx, graft::Output& output);

};

GRAFTLET_EXPORTS_BEGIN("walletAddress", GRAFTLET_MKVER(1,1));
GRAFTLET_PLUGIN(WalletAddress, IGraftlet, "walletAddressGL");
GRAFTLET_EXPORTS_END

GRAFTLET_PLUGIN_DEFAULT_CHECK_FW_VERSION(GRAFTLET_MKVER(0,3))

graft::Status WalletAddress::getWalletAddressHandler(const graft::Router::vars_t& vars, const graft::Input& input, graft::Context& ctx,
                                                     graft::Output& output)
{
    LOG_PRINT_L2(__FUNCTION__);

    if (ctx.local.getLastStatus() != graft::Status::None) {
        graft::supernode::request::GetWalletAddressErrorResponse err;
        err.error = string("internal error: wrong status: " + to_string((int)ctx.local.getLastStatus()));
        return graft::Status::Error;
    }

    graft::SupernodePtr supernode = ctx.global.get(CONTEXT_KEY_SUPERNODE, graft::SupernodePtr());
    if (!supernode) {
        graft::supernode::request::GetWalletAddressErrorResponse err;
        err.error = string("supernode was not setup correctly");
        return graft::Status::Error;
    }

    graft::supernode::request::GetWalletAddressResponse response;
    response.testnet = supernode->testnet();
    response.id_key = supernode->idKeyAsString();
    response.wallet_public_address = supernode->walletAddress();

    crypto::signature sign;
    std::string data = supernode->walletAddress() + ":" + supernode->idKeyAsString();
    supernode->signMessage(data, sign);
    response.signature = epee::string_tools::pod_to_hex(sign);
    output.load(response);
    return graft::Status::Ok;
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


