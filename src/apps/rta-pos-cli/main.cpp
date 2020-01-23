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
#include "cryptonote_basic/cryptonote_basic_impl.h"

#include "serialization/binary_utils.h" // dump_binary(), parse_binary()
#include "serialization/json_utils.h" // dump_json()
#include "include_base_utils.h"
#include "rta/requests/sale.h" // sale request struct
#include "rta/requests/presale.h" //
#include "rta/requests/common.h" //
#include "rta/requests/gettx.h" //
#include "rta/requests/getpaymentstatus.h"
#include "rta/requests/getpaymentdata.h"
#include "rta/requests/approvepaymentrequest.h"
#include "rta/requests/posrejectpaymentrequest.h"
#include "rta/requests/updatepaymentstatus.h"

#include "rta/fullsupernodelist.h"

#include "supernode/requestdefines.h"
#include "utils/cryptmsg.h" // one-to-many message cryptography
#include "utils/utils.h" // decode amount from tx
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
    
    static const std::map<std::string, std::string> id_to_address = {
        {"c89cd66f1f0e8f0dd108be9463a28e714eb513a56f0512fbce151d0c216e8941", "graft-dev01.graft:28690"},
        {"04eb530ad9b7df66786abe63cffc1c4fc0f22a8c34fccfd670390e7b1c0e8a6f", "graft-dev02.graft:28690"},
        {"54a6418ead8cf211555eb685a6574adfd1b3acb4cd0034780ba973ada17b28d1", "graft-dev03.graft:28690"},
        {"a76dd51a5007e509d81bf5aab17ac5abca04ac905aafaf83a9792fb4dcda39de", "graft-dev04.graft:28690"},
        {"9316ace1d6b31e77b2d8875c54719ef6ab0f0ce20880f1130c8b08f2618ee218", "graft-dev05.graft:28690"},
        {"07b8bdcc940ee2e67562a3c5a6291edf187170c2f31a66867760ff101f35cc87", "graft-dev06.graft:28690"},
        {"5122d63790ebf8e72f7935018ac64e2cdc760c24c3ccf0de614a1b3d7f29f759", "graft-dev07.graft:28690"},
        {"bc6eb0403452b114ad37d791b04c5d2eaa71e1a46776edfed493dd7fde839ac0", "graft-dev08.graft:28690"},
        {"1b3730554d524ed8888d8431791da9f5428722f9b2205778d9671a4886c714c1", "graft-dev09.graft:28690"}
    };
    
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
        address_parse_info address_pi;
        if (!cryptonote::get_account_address_from_str(address_pi, cryptonote::TESTNET, m_wallet_address)) {
            throw std::runtime_error("failed to parse wallet address ");
        }
        m_account = address_pi.address;
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

    static uint64_t get_total_fee(uint64_t tx_amount)
    {
        return tx_amount - (tx_amount / 1000 * 5);
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


        // encrypt purchase details only with wallet key
        request::PaymentInfo payment_info;
        payment_info.Amount = amount;
        std::string encryptedPurchaseDetails;
        graft::crypto_tools::encryptMessage(SALE_ITEMS, wallet_pub_key_vector, encryptedPurchaseDetails);
        payment_info.Details = epee::string_tools::buff_to_hex_nodelimer(encryptedPurchaseDetails);
        // encrypt whole container with auth_sample keys + wallet key;
        // TODO: by the specs it should be only encrypted with auth sample keys, auth sample should return
        // plain-text amount and encrypted payment details; for simplicity we just add wallet key to the one-to-many scheme
        std::string encrypted_payment_blob;
        auth_sample_pubkeys.push_back(wallet_pub_key);
        graft::crypto_tools::encryptMessage(graft::to_json_str(payment_info), auth_sample_pubkeys, encrypted_payment_blob);

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
    
    bool checkForSaleData( std::chrono::seconds timeout)
    {
        request::PaymentDataRequest req;
        req.BlockHash = m_presale_resp.BlockHash;
        req.PaymentID = m_payment_id;
        req.BlockHeight = m_presale_resp.BlockNumber;
        
        std::string raw_resp;
        int http_status = 0;
        ErrorResponse err_resp;
        chrono::steady_clock::time_point start = chrono::steady_clock::now();
        chrono::milliseconds elapsed;
        
        std::set<string> good_nodes, bad_nodes;
        MINFO("checking for payment data: " << m_payment_id);
        do {
            for (const auto & item : m_presale_resp.AuthSample) {
                if (good_nodes.find(item) != good_nodes.end())
                    continue;
                
                epee::net_utils::http::http_simple_client http_client;
                boost::optional<epee::net_utils::http::login> login{};
                http_client.set_server(id_to_address.at(item), login);
                
                bool r = invoke_http_rest("/dapi/v2.0/get_payment_data", req, raw_resp, err_resp, http_client, http_status, m_network_timeout, "POST");
                if (!r) {
                    MERROR("Failed to invoke get_payment_data: " << graft::to_json_str(err_resp));
                    return false;
                }       
                
                if (http_status == 200) {
                    MINFO("Payment data received from: " << item);
                    good_nodes.insert(item);
                    auto it = bad_nodes.find(item);
                    if (it != bad_nodes.end())
                        bad_nodes.erase(it);
                    
                } else {
                    MERROR("Payment data missing from: " << item);
                    bad_nodes.insert(item);
                }
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(chrono::steady_clock::now() - start);

        } while (/*elapsed < timeout && good_nodes.size() < m_presale_resp.AuthSample.size()*/false);
        
        if (good_nodes.size() != m_presale_resp.AuthSample.size()) {
            std::ostringstream oss;
            for(auto& it : bad_nodes) { 
                oss << it << "\n"; 
            }
            
            MERROR("supernodes missing sale data: \n" << oss.str());
            return false;
        }
        return true;
    }

    bool waitForStatus(int expectedStatus, int &receivedStatus, std::chrono::seconds timeout)
    {

        request::PaymentStatusRequest req;
        req.PaymentID = paymentId();

        std::string raw_resp;
        int http_status = 0;
        ErrorResponse err_resp;
        chrono::steady_clock::time_point start = chrono::steady_clock::now();
        chrono::milliseconds elapsed;

        do {

            MWARNING("Requesting payment status for payment: " << paymentId());

            bool r = invoke_http_rest("/dapi/v2.0/get_payment_status", req, raw_resp, err_resp, m_http_client, http_status, m_network_timeout, "POST");
            if (!r) {
                MERROR("Failed to invoke get_payment_status: " << graft::to_json_str(err_resp));
            }

            if (http_status == 200 && !raw_resp.empty()) {
                request::PaymentStatusResponse resp;
                if (!graft::from_json_str(raw_resp, resp)) {
                    MERROR("Failed to parse payment status response: " << raw_resp);
                    return false;
                }

                receivedStatus = resp.Status;
                if (expectedStatus == receivedStatus) {
                    return true;
                }

                if (graft::isFiniteRtaStatus((graft::RTAStatus)receivedStatus)) {
                    return false;
                }
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(chrono::steady_clock::now() - start);

        } while (elapsed < timeout);

        return  false;
    }

    bool requestTx()
    {
        request::GetTxRequest req;
        crypto::signature sign;
        crypto::hash hash;

        req.PaymentID = paymentId();
        crypto::cn_fast_hash(req.PaymentID.data(), req.PaymentID.size(), hash);
        crypto::generate_signature(hash, m_pub_key, m_secret_key, sign);
        req.Signature = epee::string_tools::pod_to_hex(sign);
        std::string raw_resp;
        int http_status = 0;
        ErrorResponse err_resp;

        bool r = invoke_http_rest("/dapi/v2.0/get_tx", req, raw_resp, err_resp, m_http_client, http_status, m_network_timeout, "POST");
        if (!r) {
            MERROR("Failed to invoke get_tx: " << graft::to_json_str(err_resp));
            return r;
        }

        GetTxResponse resp;
        if (!graft::from_json_str(raw_resp, resp)) {
            MERROR("Failed to parse tx response: " << raw_resp);
            return false;
        }


        if (!utils::decryptTxFromHex(resp.TxBlob, m_secret_key, m_tx)) {
            MERROR("Failed to decrypt tx");
            return false;
        }

        if (!utils::decryptTxKeyFromHex(resp.TxKeyBlob, m_secret_key, m_txkey)) {
            MERROR("Failed to decrypt tx key");
            return false;
        }
        return true;

    }

    bool validateTx(uint64_t sale_amount)
    {
        std::vector<std::pair<size_t, uint64_t>> outputs;
        uint64_t tx_amount = 0;

        if (!Utils::get_tx_amount(m_account, m_txkey, m_tx, outputs, tx_amount)) {
            MERROR("Failed to get amount from tx");
            return false;
        }


        MINFO("sale amount: " << sale_amount << ", tx_amount : " << tx_amount << ", expected amount: " << get_total_fee(sale_amount));
        return tx_amount == get_total_fee(sale_amount); //
    }

    bool approvePayment()
    {
        ApprovePaymentRequest req;

        std::vector<cryptonote::rta_signature> rta_signatures;
        cryptonote::rta_header rta_hdr;

        if (!cryptonote::get_graft_rta_header_from_extra(m_tx, rta_hdr)) {
            MERROR("Failed to read rta_hdr from tx");
            return false;
        }

        rta_signatures.resize(graft::FullSupernodeList::AUTH_SAMPLE_SIZE + 3);
        crypto::signature &sig = rta_signatures.at(cryptonote::rta_header::POS_KEY_INDEX).signature;
        crypto::hash hash = cryptonote::get_transaction_hash(m_tx);
        crypto::generate_signature(hash, m_pub_key, m_secret_key, sig);

        m_tx.extra2.clear();
        cryptonote::add_graft_rta_signatures_to_extra2(m_tx.extra2, rta_signatures);
        utils::encryptTxToHex(m_tx, rta_hdr.keys, req.TxBlob);

        std::string raw_resp;
        int http_status = 0;
        ErrorResponse err_resp;

        bool r = invoke_http_rest("/dapi/v2.0/approve_payment", req, raw_resp, err_resp, m_http_client, http_status, m_network_timeout, "POST");
        if (!r) {
            MERROR("Failed to invoke approve_payment: " << graft::to_json_str(err_resp));
            return r;
        }
        MINFO("approve_payment returned: " << raw_resp);
        return true;
    }

    bool rejectPayment()
    {
        EncryptedPaymentStatus req;
        PaymentStatus paymentStatus;
        paymentStatus.Status = static_cast<int>(graft::RTAStatus::FailRejectedByPOS);
        paymentStatus.PaymentID = m_payment_id;
        paymentStatusSign(m_pub_key, m_secret_key, paymentStatus);


        MDEBUG("about to send status update: " <<  graft::to_json_str(paymentStatus));

        cryptonote::rta_header rta_hdr;

        if (!cryptonote::get_graft_rta_header_from_extra(m_tx, rta_hdr)) {
            MERROR("Failed to read rta_hdr from tx");
            return false;
        }


        req.PaymentID = m_payment_id;
        paymentStatusEncrypt(paymentStatus, rta_hdr.keys, req);

        std::string raw_resp;
        int http_status = 0;
        ErrorResponse err_resp;

        bool r = invoke_http_rest("/dapi/v2.0/pos_reject_payment", req, raw_resp, err_resp, m_http_client, http_status, m_network_timeout, "POST");
        if (!r) {
            MERROR("Failed to invoke pos_reject_payment: " << graft::to_json_str(err_resp));
            return r;
        }

        MINFO("pos_reject_payment returned: " << raw_resp);
        return true;
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
    cryptonote::account_public_address m_account;
    std::string m_payment_id;
    PresaleResponse m_presale_resp;
    cryptonote::transaction m_tx;
    crypto::secret_key m_txkey;
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
    const command_line::arg_descriptor<std::string> arg_pos_wallet_address = { "wallet-address", "POS Wallet address", "", false };


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
    
    // debugging p2p
    MINFO("Checking for payment data... ");
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    pos.checkForSaleData(std::chrono::seconds(20));
    return EXIT_SUCCESS;


    MINFO("Sale initiated: " << pos.paymentId());

    int actualStatus = 0;
    if (!pos.waitForStatus(int(graft::RTAStatus::InProgress), actualStatus, std::chrono::seconds(20))) {
        MERROR("Expected in-progress status, got: " << actualStatus);
        return EXIT_FAILURE;
    }

    MINFO("Sale status changed: " << int(graft::RTAStatus::InProgress) << ", " << pos.paymentId());
    // TODO: request tx, validate it
    if (!pos.requestTx()) {
        return EXIT_FAILURE;
    }

    MINFO("Sale tx received for payment: " << pos.paymentId());

    if (!pos.validateTx(amount)) {
        return EXIT_FAILURE;
    }

    MINFO("Sale tx validated for payment: " << pos.paymentId());
    std::this_thread::sleep_for(std::chrono::milliseconds(5000));
#if 1
    if (!pos.approvePayment()) {
        return EXIT_FAILURE;
    }

    // wait for status = Success;
    if (!pos.waitForStatus(int(graft::RTAStatus::Success), actualStatus, std::chrono::seconds(20))) {
        MERROR("Expected Success status, got: " << actualStatus);
        return EXIT_FAILURE;
    }

#endif

#if 0
    if (!pos.rejectPayment()) {
        return EXIT_FAILURE;
    }

    // wait for status = Success;
    if (!pos.waitForStatus(int(graft::RTAStatus::FailRejectedByPOS), actualStatus, std::chrono::seconds(20))) {
        MERROR("Expected RejectedByPOS status, got: " << actualStatus);
        return EXIT_FAILURE;
    }
#endif

    MWARNING("Payment: " << pos.paymentId() << "  processed, status: " << actualStatus);

    return 0;

    CATCH_ENTRY("App error", 1);
}
