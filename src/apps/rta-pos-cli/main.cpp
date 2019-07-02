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


namespace
{
    std::string generate_payment_id(const crypto::public_key &pubkey)
    {
        boost::uuids::basic_random_generator<boost::mt19937> gen;
        boost::uuids::uuid u = gen();
        const std::string msg = epee::string_tools::pod_to_hex(pubkey) + boost::uuids::to_string(u);
        crypto::hash h = crypto::cn_fast_hash(msg.data(), msg.length());
        return epee::string_tools::pod_to_hex(h);
    }

    template<class t_request, class t_error, class t_transport>
    bool invoke_http_rest(const boost::string_ref uri, const t_request& req, std::string &response,
                          t_error& error_struct, t_transport& transport, int &status_code, std::chrono::milliseconds timeout = std::chrono::seconds(15),
                          const boost::string_ref method = "GET")
    {
        graft::Output out;
        out.load(req);

        epee::net_utils::http::fields_list additional_params;
        additional_params.push_back(std::make_pair("Content-Type","application/json; charset=utf-8"));

        const epee::net_utils::http::http_response_info* pri = NULL;
        if(!transport.invoke(uri, method, out.data(), timeout, std::addressof(pri), std::move(additional_params)))
        {
            LOG_PRINT_L1("Failed to invoke http request to  " << uri);
            return false;
        }

        if(!pri)
        {
            LOG_PRINT_L1("Failed to invoke http request to  " << uri << ", internal error (null response ptr)");
            return false;
        }
        status_code = pri->m_response_code;
        response = pri->m_body;

        if(pri->m_response_code/100 != 2)
        {
            graft::Input in; in.load(response);
            LOG_PRINT_L1("Failed to invoke http request to  " << uri << ", wrong response code: " << pri->m_response_code);
            in.get(error_struct);
            return false;
        }

        return true;

    }

    template<class t_request, class t_response, class t_error, class t_transport>
    bool invoke_http_rest(const boost::string_ref uri, const t_request& req, t_response& result_struct, t_error& error_struct, t_transport& transport, int &status_code, std::chrono::milliseconds timeout = std::chrono::seconds(15), const boost::string_ref method = "GET")
    {
        std::string response_body;
        if (!invoke_http_rest(uri, req, response_body, error_struct, transport, status_code, timeout, method)) {
            MERROR("invoke_http_rest failed: " << response_body);
            return false;
        }
        graft::Input in; in.load(response_body);
        return in.get(result_struct);
    }


    template <typename Req>
    std::string as_json_str(const Req &request)
    {
        graft::Output out;
        out.load(request);
        return out.data();
    }

    static const std::string SALE_ITEMS = "SERIALIZED SALE ITEMS HERE";

}

namespace po = boost::program_options;
using namespace cryptonote;
using namespace epee;
using namespace std;
using namespace graft::supernode;

//=======================================================



int main(int argc, char* argv[])
{
    TRY_ENTRY();
    epee::string_tools::set_module_name_and_folder(argv[0]);

    uint32_t log_level = 0;
    std::string m_config_folder;

    tools::on_startup();

    po::options_description desc_cmd("Command line options");
    //  desc_cmd.add_options()
    //       ("help,h", "Help screen")
    //       ("qrcode-file", po::value<string>()->default_value("wallet-qr-code.json"), "Specify file where to write qr-code")
    //       ("age", po::value<int>()->notifier(on_age), "Age");



    const command_line::arg_descriptor<std::string> arg_input_file = {"qrcode-file", "Specify file where to write qr-code", "wallet-qr-code.json", false};
    const command_line::arg_descriptor<std::string> arg_log_level   = {"log-level",  "0-4 or categories", "", false};
    const command_line::arg_descriptor<std::string> arg_amount  = {"amount", "Sale amount", "12.345", false};
    const command_line::arg_descriptor<std::string> arg_sale_items_file  = {"sale-items-file", "File where to read sale items", "sale-items.json", false};
    const command_line::arg_descriptor<size_t>        arg_sale_timeout  = {"sale-timeout", "Sale timeout in millis", 5000, false};
    const command_line::arg_descriptor<std::string>     arg_supernode_address = {  "supernode-address", "Supernode address", "localhost:28900", false };


    command_line::add_arg(desc_cmd, arg_input_file);
    command_line::add_arg(desc_cmd, arg_log_level);
    command_line::add_arg(desc_cmd, arg_amount);

    command_line::add_arg(desc_cmd, arg_sale_items_file);
    command_line::add_arg(desc_cmd, arg_sale_timeout);
    command_line::add_arg(desc_cmd, arg_supernode_address);
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
    std::cout << "sale-items-file: "   << command_line::get_arg(vm, arg_sale_items_file) << std::endl;
    std::cout << "sale-itimeout: "     << command_line::get_arg(vm, arg_sale_timeout) << std::endl;
    std::cout << "supernode-address: " << command_line::get_arg(vm, arg_supernode_address) << std::endl;

    uint64_t amount = 0;
    std::string amount_str = command_line::get_arg(vm, arg_amount);
    if (!cryptonote::parse_amount(amount, amount_str)) {
        MERROR("Failed to parse amount : " << amount_str);
        return 1;
    }

    // 1. generate one-time keypair and payment id base;
    crypto::public_key pub_key;
    crypto::secret_key secret_key;
    crypto::generate_keys(pub_key, secret_key);
    std::string payment_id = generate_payment_id(pub_key);

    // 2. call presale, process response
    epee::net_utils::http::http_simple_client http_client;
    boost::optional<epee::net_utils::http::login> login{};
    http_client.set_server(command_line::get_arg(vm, arg_supernode_address), login);
    std::chrono::seconds rpc_timeout = std::chrono::seconds(10);
    request::PresaleRequest presale_req;
    presale_req.PaymentID = payment_id;
    request::PresaleResponse presale_resp;
    ErrorResponse err_resp;
    int http_status = 0;

    r = invoke_http_rest("/dapi/v2.0/presale", presale_req, presale_resp, err_resp, http_client, http_status, rpc_timeout, "POST");
    if (!r) {
        MERROR("Failed to invoke presale: " << as_json_str(err_resp));
        return 1;
    }

    MDEBUG("response: " << as_json_str(presale_resp));

    // 3. call sale
    // 3.1. build pay/sale info with amount
    request::SaleRequest sale_req;
    request::PaymentInfo payment_info;
    payment_info.Details = SALE_ITEMS;
    payment_info.Amount = amount;
    // 3.2 encrypt it using one-to-many scheme
    graft::Output out; out.load(payment_info);
    std::vector<crypto::public_key> auth_sample_pubkeys;

    for (const auto &key_str : presale_resp.AuthSample) {
        crypto::public_key pkey;
        if (!epee::string_tools::hex_to_pod(key_str, pkey)) {
            MERROR("Failed to parse public key from : " << key_str);
            return 1;
        }
        auth_sample_pubkeys.push_back(pkey);
        graft::supernode::request::EncryptedNodeKey item;
        item.Id = key_str;
        sale_req.paymentData.AuthSampleKeys.push_back(item);
    }
    std::string encrypted_payment_blob;

    graft::crypto_tools::encryptMessage(out.data(), auth_sample_pubkeys, encrypted_payment_blob);
    // 3.3. Set pos proxy addess and wallet in sale request
    sale_req.paymentData.PosProxy = presale_resp.PosProxy;
    sale_req.paymentData.EncryptedPayment = epee::string_tools::buff_to_hex_nodelimer(encrypted_payment_blob);
    sale_req.PaymentID = payment_id;

    // 3.4. call "/sale"
    std::string dummy;
    r = invoke_http_rest("/dapi/v2.0/sale", sale_req, dummy, err_resp, http_client, http_status, rpc_timeout, "POST");
    if (!r) {
        MERROR("Failed to invoke sale: " << as_json_str(err_resp));
        return 1;
    }

    MDEBUG("/sale response status: " << http_status);

    // 4. poll for status change
    // TODO

    return 0;

    CATCH_ENTRY("App error", 1);
}
