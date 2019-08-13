// Copyright (c) 2019, The Graft Project
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

#include "supernode/requestdefines.h"
#include "lib/graft/graft_exception.h"

// client endpoints
#include "requests/presale.h"
#include "requests/sale.h"
#include "requests/pay.h"
#include "requests/getpaymentdata.h"
#include "requests/gettx.h"
#include "requests/approvepaymentrequest.h"
#include "requests/posrejectpaymentrequest.h"

// core endpoints
#include "requests/storepaymentdata.h"
#include "requests/paymentdatarequest.h"
#include "requests/paymentdataresponse.h"
#include "requests/getsupernodeinfo.h"
#include "requests/authorizertatx.h"
#include "requests/updatepaymentstatus.h"
#include "requests/getpaymentstatus.h"

// internal endpoints
#include "requests/send_raw_tx.h"


#include <rta/supernode.h>
#include <rta/fullsupernodelist.h>
#include <lib/graft/ConfigIni.h>

#include <boost/filesystem.hpp>


#undef MONERO_DEFAULT_LOG_CATEGORY
#define MONERO_DEFAULT_LOG_CATEGORY "graftlet.WalletAddress"

using namespace graft;

class RtaGraftlet: public IGraftlet
{
public:
    RtaGraftlet(const char* name)
      : IGraftlet(name) { }

protected:
    virtual void initOnce(const graft::CommonOpts& opts, graft::Context& ctx) override
    {
        prepareSupernode(opts, ctx);
        REGISTER_ENDPOINT("/dapi/v2.0/test", METHOD_GET, RtaGraftlet, test);
        REGISTER_ENDPOINT("/dapi/v2.0/presale", METHOD_POST, RtaGraftlet, handlePresaleRequest);
        REGISTER_ENDPOINT("/dapi/v2.0/sale", METHOD_POST, RtaGraftlet, handleSaleRequest);
        REGISTER_ENDPOINT("/dapi/v2.0/pay", METHOD_POST, RtaGraftlet, handlePayRequest);
        REGISTER_ENDPOINT("/dapi/v2.0/get_payment_data", METHOD_POST, RtaGraftlet, handleGetPaymentDataRequest);
        REGISTER_ENDPOINT("/dapi/v2.0/get_payment_status", METHOD_POST, RtaGraftlet, handleGetPaymentStatusRequest);
        REGISTER_ENDPOINT("/dapi/v2.0/get_tx", METHOD_POST, RtaGraftlet, handleGetTxRequest);
        REGISTER_ENDPOINT("/dapi/v2.0/approve_payment", METHOD_POST, RtaGraftlet, handleApprovePaymentRequest);
        REGISTER_ENDPOINT("/dapi/v2.0/pos_reject_payment", METHOD_POST, RtaGraftlet, handlePosRejectPaymentRequest);

        // TODO: fix cryptonode logic, it prepends "/dapi/v2.0/" to any "callback_uri"
        REGISTER_ENDPOINT("/dapi/v2.0/core/payment_data_request", METHOD_POST, RtaGraftlet, handlePaymentDataRequest);
        REGISTER_ENDPOINT("/dapi/v2.0/core/payment_data_response", METHOD_POST, RtaGraftlet, handlePaymentDataResponse);
        REGISTER_ENDPOINT("/dapi/v2.0/core/store_payment_data", METHOD_POST, RtaGraftlet, handleStorePaymentDataRequest);
        REGISTER_ENDPOINT("/dapi/v2.0/core/get_supernode_info", METHOD_POST, RtaGraftlet, handleSupernodeInfoRequest);
        REGISTER_ENDPOINT("/dapi/v2.0/core/update_payment_status", METHOD_POST, RtaGraftlet, handleUpdatePaymentStatusRequest);
        REGISTER_ENDPOINT("/dapi/v2.0/core/update_payment_status_encrypted", METHOD_POST, RtaGraftlet, handleUpdatePaymentStatusEncryptedRequest);
        REGISTER_ENDPOINT("/dapi/v2.0/core/authorize_rta_tx_request", METHOD_POST, RtaGraftlet, handleAuthorizeRtaTxRequest);
        REGISTER_ENDPOINT("/dapi/v2.0/core/authorize_rta_tx_response", METHOD_POST, RtaGraftlet, handleAuthorizeRtaTxResponse);
        REGISTER_ENDPOINT("/cryptonode/sendrawtx", METHOD_POST, RtaGraftlet, handleSendRawTx);

    }

private:

    void prepareSupernode(const graft::CommonOpts& opts, graft::Context& ctx)
    {
        // create data directory if not exists
        boost::filesystem::path data_path(opts.data_dir);

        if (!boost::filesystem::exists(data_path)) {
            boost::system::error_code ec;
            if (!boost::filesystem::create_directories(data_path, ec)) {
                throw std::runtime_error(ec.message());
            }
        }

        // read config
        auto config = graft::ConfigIniSubtree::create(opts.config_filename);
        std::string cryptonode_rpc_address = config.get<std::string>("cryptonode.rpc-address");
        std::string supernode_http_address = config.get<std::string>("server.http-address");

        // create supernode instance and put it into global context
        graft::SupernodePtr supernode = boost::make_shared<graft::Supernode>(
                        opts.wallet_public_address,
                        crypto::public_key(),
                        cryptonode_rpc_address,
                        opts.testnet
                        );

        std::string keyfilename = (data_path / "supernode.keys").string();
        if (!supernode->loadKeys(keyfilename)) {
            // supernode is not initialized, generating key
            supernode->initKeys();
            if (!supernode->saveKeys(keyfilename)) {
                MERROR("Failed to save keys");
                throw std::runtime_error("Failed to save keys");
            }
        }
        // TODO: why path is needed here? it's not a part of the "network address" (which is normally hostname:port)
        supernode->setNetworkAddress(supernode_http_address + "/dapi/v2.0");

        // create fullsupernode list instance and put it into global context
        graft::FullSupernodeListPtr fsl = boost::make_shared<graft::FullSupernodeList>(
                    cryptonode_rpc_address,
                    opts.testnet);
        fsl->add(supernode);

        ctx.global[CONTEXT_KEY_SUPERNODE] = supernode;
        ctx.global[CONTEXT_KEY_FULLSUPERNODELIST] = fsl;
        // TODO: check what depends on following context values, move to the proper place;
        ctx.global["testnet"] = opts.testnet;
        ctx.global["cryptonode_rpc_address"] = cryptonode_rpc_address;
        ctx.global["supernode_url"] = supernode_http_address + "/dapi/v2.0";
    }

    Status test(const Router::vars_t& vars, const graft::Input& input,
                            graft::Context& ctx, graft::Output& output)
    {
        output.body = "Test";
        return Status::Ok;
    }
    // client (dapi) endpoints
    Status handlePresaleRequest(const Router::vars_t& vars, const graft::Input& input, graft::Context& ctx, graft::Output& output)
    {
        return graft::supernode::request::handlePresaleRequest(vars, input, ctx, output);
    }

    Status handleSaleRequest(const Router::vars_t& vars, const graft::Input& input, graft::Context& ctx, graft::Output& output)
    {
        return graft::supernode::request::handleSaleRequest(vars, input, ctx, output);
    }

    Status handlePayRequest(const Router::vars_t& vars, const graft::Input& input, graft::Context& ctx, graft::Output& output)
    {
        return graft::supernode::request::handlePayRequest(vars, input, ctx, output);
    }

    Status handleGetPaymentDataRequest(const Router::vars_t& vars, const graft::Input& input, graft::Context& ctx, graft::Output& output)
    {
        return graft::supernode::request::getPaymentDataRequest(vars, input, ctx, output);
    }


    Status handleGetPaymentStatusRequest(const Router::vars_t& vars, const graft::Input& input, graft::Context& ctx, graft::Output& output)
    {
        return graft::supernode::request::getPaymentStatusRequest(vars, input, ctx, output);
    }

    Status handleGetTxRequest(const Router::vars_t& vars, const graft::Input& input, graft::Context& ctx, graft::Output& output)
    {
        return graft::supernode::request::getTxRequest(vars, input, ctx, output);
    }

    Status handleApprovePaymentRequest(const Router::vars_t& vars, const graft::Input& input, graft::Context& ctx, graft::Output& output)
    {
        return graft::supernode::request::handleApprovePaymentRequest(vars, input, ctx, output);
    }

    Status handlePosRejectPaymentRequest(const Router::vars_t& vars, const graft::Input& input, graft::Context& ctx, graft::Output& output)
    {
        return graft::supernode::request::handlePosRejectPaymentRequest(vars, input, ctx, output);
    }




    // core endpoints
    Status handleStorePaymentDataRequest(const Router::vars_t& vars, const graft::Input& input, graft::Context& ctx, graft::Output& output)
    {
        return graft::supernode::request::storePaymentDataRequest(vars, input, ctx, output);
    }

    Status handlePaymentDataRequest(const Router::vars_t& vars, const graft::Input& input, graft::Context& ctx, graft::Output& output)
    {
        return graft::supernode::request::paymentDataRequest(vars, input, ctx, output);
    }

    Status handlePaymentDataResponse(const Router::vars_t& vars, const graft::Input& input, graft::Context& ctx, graft::Output& output)
    {
        return graft::supernode::request::paymentDataResponse(vars, input, ctx, output);
    }

    Status handleSupernodeInfoRequest(const Router::vars_t& vars, const graft::Input& input, graft::Context& ctx, graft::Output& output)
    {
        return graft::supernode::request::handleSupernodeInfoRequest(vars, input, ctx, output);
    }

    Status handleAuthorizeRtaTxRequest(const Router::vars_t& vars, const graft::Input& input, graft::Context& ctx, graft::Output& output)
    {
        return graft::supernode::request::handleAuthorizeRtaTxRequest(vars, input, ctx, output);
    }

    Status handleAuthorizeRtaTxResponse(const Router::vars_t& vars, const graft::Input& input, graft::Context& ctx, graft::Output& output)
    {
        return graft::supernode::request::handleAuthorizeRtaTxResponse(vars, input, ctx, output);
    }

    Status handleUpdatePaymentStatusRequest(const Router::vars_t& vars, const graft::Input& input, graft::Context& ctx, graft::Output& output)
    {
        return graft::supernode::request::handleUpdatePaymentStatusRequest(vars, input, ctx, output);
    }

    Status handleUpdatePaymentStatusEncryptedRequest(const Router::vars_t& vars, const graft::Input& input, graft::Context& ctx, graft::Output& output)
    {
        return graft::supernode::request::handleUpdatePaymentStatusRequestEx(vars, input, ctx, output);
    }

    Status handleSendRawTx(const Router::vars_t& vars, const graft::Input& input, graft::Context& ctx, graft::Output& output)
    {
        return graft::supernode::request::sendRawTxHandler(vars, input, ctx, output);
    }





};

GRAFTLET_EXPORTS_BEGIN("RTA", GRAFTLET_MKVER(1,1));
GRAFTLET_PLUGIN(RtaGraftlet, IGraftlet, "RTA");
GRAFTLET_EXPORTS_END

GRAFTLET_PLUGIN_DEFAULT_CHECK_FW_VERSION(GRAFTLET_MKVER(0,3))

