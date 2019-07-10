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

#include "common.h"
#include "misc_log_ex.h"
#include "cryptonote_basic/cryptonote_format_utils.h"
#include "cryptonote_core/cryptonote_core.h"
#include "serialization/binary_utils.h" // dump_binary(), parse_binary()
#include "serialization/json_utils.h" // dump_json()
#include "include_base_utils.h"
#include "rta/requests/sale.h" // sale request struct
#include "rta/requests/presale.h" //
#include "rta/requests/common.h" //
#include "supernode/requestdefines.h"
#include "utils/cryptmsg.h" // one-to-many message cryptography
#include "lib/graft/serialize.h"


#include <net/http_client.h>
#include <storages/http_abstract_invoke.h>



#include <atomic>
#include <cstdio>
#include <algorithm>
#include <string>
#include <fstream>

#include <boost/filesystem.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/uuid/uuid.hpp>
#include <unistd.h>


#undef MONERO_DEFAULT_LOG_CATEGORY
#define MONERO_DEFAULT_LOG_CATEGORY "rta-pos-cli"

using namespace graft::supernode::request;
using namespace graft::rta::apps;


namespace
{

    static const std::string POS_WALLET_ADDRESS = "F8LHnvFd4EUM9dmoKPoBWiT6BoHbywXxjEwLiidRseH72cejWqy9frB2nxN1soKXUWJhAiZSgaJsfWaeLeTRdoN18RpRVsb";

    std::string generate_payment_id(const crypto::public_key &pubkey)
    {
        boost::uuids::basic_random_generator<boost::mt19937> gen;
        boost::uuids::uuid u = gen();
        const std::string msg = epee::string_tools::pod_to_hex(pubkey) + boost::uuids::to_string(u);
        crypto::hash h = crypto::cn_fast_hash(msg.data(), msg.length());
        return epee::string_tools::pod_to_hex(h);
    }

    static const std::string SALE_ITEMS = "SERIALIZED SALE ITEMS HERE";

}

namespace po = boost::program_options;
using namespace cryptonote;
using namespace epee;
using namespace std;
using namespace graft::supernode;

//=======================================================

class PoS
{
public:
    PoS(const std::string &qrcode_file, const std::string &supernode_address, const std::string &sale_items_file,
        uint64_t sale_timeout, const std::string &wallet_address)
        : m_qrcode_file(qrcode_file)
        , m_sale_items_file(sale_items_file)
        , m_sale_timeout(std::chrono::milliseconds(sale_timeout))
        , m_wallet_address(wallet_address)

    {
        boost::optional<epee::net_utils::http::login> login{};
        m_http_client.set_server(supernode_address, login);
    }

    bool presale()
    {
        crypto::generate_keys(m_pub_key, m_secret_key);
        m_payment_id = generate_payment_id(m_pub_key);
        request::PresaleRequest presale_req;
        presale_req.PaymentID = m_payment_id;
        ErrorResponse err_resp;
        int http_status = 0;

        bool r = invoke_http_rest("/dapi/v2.0/presale", presale_req, m_presale_resp, err_resp, m_http_client, http_status, m_network_timeout, "POST");
        if (!r) {
            MERROR("Failed to invoke presale: " << graft::to_json_str(err_resp));
        } else {
            MDEBUG("response: " << graft::to_json_str(m_presale_resp));
        }
        return r;
    }

    bool sale(uint64_t amount)
    {
        request::SaleRequest sale_req;

        // 3.2 encrypt it using one-to-many scheme

        std::vector<crypto::public_key> auth_sample_pubkeys;

        for (const auto &key_str : m_presale_resp.AuthSample) {
            crypto::public_key pkey;
            if (!epee::string_tools::hex_to_pod(key_str, pkey)) {
                MERROR("Failed to parse public key from : " << key_str);
                return 1;
            }
            auth_sample_pubkeys.push_back(pkey);
            graft::supernode::request::NodeId item;
            item.Id = key_str;
            sale_req.paymentData.AuthSampleKeys.push_back(item);
        }

        // extra keypair for a wallet, instead of passing session key
        crypto::public_key wallet_pub_key;
        crypto::generate_keys(wallet_pub_key, m_wallet_secret_key);
        std::vector<crypto::public_key> wallet_pub_key_vector {wallet_pub_key};

        // encrypt purchase details with wallet key
        request::PaymentInfo payment_info;
        payment_info.Amount = amount;
        std::string encryptedPurchaseDetails;
        graft::crypto_tools::encryptMessage(SALE_ITEMS, wallet_pub_key_vector, encryptedPurchaseDetails);
        payment_info.Details = epee::string_tools::buff_to_hex_nodelimer(encryptedPurchaseDetails);
        graft::Output out; out.load(payment_info);
        std::string encrypted_payment_blob;
        graft::crypto_tools::encryptMessage(out.data(), auth_sample_pubkeys, encrypted_payment_blob);
        // 3.3. Set pos proxy addess and wallet in sale request
        sale_req.paymentData.PosProxy = m_presale_resp.PosProxy;
        sale_req.paymentData.EncryptedPayment = epee::string_tools::buff_to_hex_nodelimer(encrypted_payment_blob);
        sale_req.PaymentID = m_payment_id;

        // 3.4. call "/sale"
        std::string dummy;
        int http_status = 0;
        ErrorResponse err_resp;

        bool r = invoke_http_rest("/dapi/v2.0/sale", sale_req, dummy, err_resp, m_http_client, http_status, m_network_timeout, "POST");
        if (!r) {
            MERROR("Failed to invoke sale: " << graft::to_json_str(err_resp));
        }

        MDEBUG("/sale response status: " << http_status);
        return r;
    }

    bool saveQrCodeFile()
    {
        WalletDataQrCode qrCode;
        qrCode.blockHash = m_presale_resp.BlockHash;
        qrCode.blockNumber = m_presale_resp.BlockNumber;
        qrCode.key = epee::string_tools::pod_to_hex(m_wallet_secret_key);
        qrCode.posAddress.Id = epee::string_tools::pod_to_hex(m_pub_key);
        qrCode.posAddress.WalletAddress = m_wallet_address;
        qrCode.paymentId = paymentId();

        bool r = epee::file_io_utils::save_string_to_file(m_qrcode_file, graft::to_json_str(qrCode));
        if (!r) {
            MERROR("Failed to write qrcode file");

        }
        return r;
    }

    const std::string & paymentId() const
    {
        return m_payment_id;
    }




private:
    std::string m_qrcode_file;

    std::string m_sale_items_file;
    std::chrono::microseconds m_sale_timeout = std::chrono::milliseconds(5000);
    crypto::public_key m_pub_key;
    crypto::secret_key m_secret_key, m_wallet_secret_key;
    epee::net_utils::http::http_simple_client m_http_client;
    std::chrono::seconds m_network_timeout = std::chrono::seconds(10);
    std::string m_wallet_address;
    std::string m_payment_id;
    PresaleResponse m_presale_resp;
};




int main(int argc, char* argv[])
{
    TRY_ENTRY();
    epee::string_tools::set_module_name_and_folder(argv[0]);

    uint32_t log_level = 0;
    std::string m_config_folder;

    tools::on_startup();

    po::options_description desc_cmd("Command line options");

    const command_line::arg_descriptor<std::string> arg_input_file = {"qrcode-file", "Specify file where to write qr-code", "wallet-qr-code.json", false};
    const command_line::arg_descriptor<std::string> arg_log_level   = {"log-level",  "0-4 or categories", "", false};
    const command_line::arg_descriptor<std::string> arg_amount  = {"amount", "Sale amount", "12.345", false};
    const command_line::arg_descriptor<std::string> arg_sale_items_file  = {"sale-items-file", "File where to read sale items", "sale-items.json", false};
    const command_line::arg_descriptor<size_t>      arg_sale_timeout  = {"sale-timeout", "Sale timeout in millis", 5000, false};
    const command_line::arg_descriptor<std::string> arg_supernode_address = { "supernode-address", "Supernode address", "localhost:28690", false };
    const command_line::arg_descriptor<std::string> arg_pos_wallet_address = { "wallet-address", "POS Wallet address", POS_WALLET_ADDRESS, false };


    command_line::add_arg(desc_cmd, arg_input_file);
    command_line::add_arg(desc_cmd, arg_log_level);
    command_line::add_arg(desc_cmd, arg_amount);

    command_line::add_arg(desc_cmd, arg_sale_items_file);
    command_line::add_arg(desc_cmd, arg_sale_timeout);
    command_line::add_arg(desc_cmd, arg_supernode_address);
    command_line::add_arg(desc_cmd, arg_pos_wallet_address);
    command_line::add_arg(desc_cmd, command_line::arg_help);

    po::options_description desc_options("Allowed options");
    desc_options.add(desc_cmd);

    po::variables_map vm;

    bool r = command_line::handle_error_helper(desc_options, [&]()
    {
        po::store(po::parse_command_line(argc, argv, desc_options), vm);
        po::notify(vm);
        return true;
    });
    if (! r)
        return 1;


    if (command_line::get_arg(vm, command_line::arg_help))
    {
        std::cout << desc_options << std::endl;
        return 1;
    }

    mlog_configure(mlog_get_default_log_path("rta-pos-cli.log"), true);
    if (!command_line::is_arg_defaulted(vm, arg_log_level))
        mlog_set_log(command_line::get_arg(vm, arg_log_level).c_str());
    else
        mlog_set_log(std::string(std::to_string(log_level) + ",rta-pos-cli:INFO").c_str());

    MINFO("Starting...");

    boost::filesystem::path fs_import_file_path;

    std::cout << "qrcode-file: " << command_line::get_arg(vm, arg_input_file) << std::endl;
    std::cout << "log-level: "   << command_line::get_arg(vm, arg_log_level) << std::endl;
    std::cout << "amount: "      << command_line::get_arg(vm, arg_amount) << std::endl;
    std::cout << "sale-items-file: "    << command_line::get_arg(vm, arg_sale_items_file) << std::endl;
    std::cout << "sale-itimeout: "      << command_line::get_arg(vm, arg_sale_timeout) << std::endl;
    std::cout << "supernode-address: "  << command_line::get_arg(vm, arg_supernode_address) << std::endl;
    std::cout << "pos-wallet-address: " << command_line::get_arg(vm, arg_pos_wallet_address) << std::endl;

    uint64_t amount = 0;
    std::string amount_str = command_line::get_arg(vm, arg_amount);
    if (!cryptonote::parse_amount(amount, amount_str)) {
        MERROR("Failed to parse amount : " << amount_str);
        return 1;
    }

    PoS pos(command_line::get_arg(vm, arg_input_file),
            command_line::get_arg(vm, arg_supernode_address),
            command_line::get_arg(vm, arg_sale_items_file),
            command_line::get_arg(vm, arg_sale_timeout),
            command_line::get_arg(vm, arg_pos_wallet_address)
            );

    if (!pos.presale()) {
        return EXIT_FAILURE;
    }

    if (!pos.sale(amount)) {
        return EXIT_FAILURE;
    }

    if (!pos.saveQrCodeFile()) {
        return EXIT_FAILURE;
    }
    MINFO("Sale initiated: " << pos.paymentId());
    // TODO:

    return 0;

    CATCH_ENTRY("App error", 1);
}
