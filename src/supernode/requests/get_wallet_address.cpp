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

#include "supernode/requests/get_wallet_address.h"
#include "supernode/requestdefines.h"
#include <misc_log_ex.h>

#undef MONERO_DEFAULT_LOG_CATEGORY
#define MONERO_DEFAULT_LOG_CATEGORY "supernode.getwalletaddressrequest"

namespace graft::supernode::request {

graft::Status getWalletAddressHandler(const Router::vars_t& vars, const graft::Input& input,
                                 graft::Context& ctx, graft::Output& output)
{
    LOG_PRINT_L2(__FUNCTION__);
    assert(ctx.local.getLastStatus() == graft::Status::None);
    bool testnet = ctx.global["testnet"];
    std::string wallet_public_address = ctx.global["wallet_public_address"];
    if(wallet_public_address.empty())
    {
        GetWalletAddressErrorResponse res;
        res.testnet = testnet;
        output.load(res);
        return graft::Status::Ok;
    }
    std::string id_key = ctx.global["wallet_id_key"];
    GetWalletAddressResponse res;
    res.testnet = testnet;
    res.wallet_public_address = wallet_public_address;
    res.id_key = id_key;
    output.load(res);
    return graft::Status::Ok;
}

void registerGetWalletAddressRequest(graft::Router& router)
{
    const char * path = "/cryptonode/getwalletaddress";
    router.addRoute(path, METHOD_GET, {nullptr, getWalletAddressHandler, nullptr} );
    LOG_PRINT_L2("route " << path << " registered");
}

}

