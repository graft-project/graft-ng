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
#define MONERO_DEFAULT_LOG_CATEGORY "rta-wallet-cli"

using namespace graft::supernode::request;
using namespace graft::rta::apps;
namespace po = boost::program_options;
using namespace cryptonote;
using namespace epee;
using namespace std;
using namespace graft::supernode;

//=======================================================
class Wallet
{
public:
    Wallet(const std::string &qrcode_file, const std::string &supernode_address,
        uint64_t sale_timeout, const std::string &wallet_file)
        : m_qrcode_file(qrcode_file)
        , m_sale_timeout(std::chrono::milliseconds(sale_timeout))
        , m_wallet_file(wallet_file)

    {
        boost::optional<epee::net_utils::http::login> login{};
        m_http_client.set_server(supernode_address, login);
    }

    bool readPaymentDetails()
    {
        return false;
    }

    bool pay()
    {
        return false;
    }

    std::string paymentId() const
    {
        return m_payment_id;
    }


private:
    std::string m_qrcode_file;
    std::chrono::microseconds m_sale_timeout = std::chrono::milliseconds(5000);
    epee::net_utils::http::http_simple_client m_http_client;
    std::chrono::seconds m_network_timeout = std::chrono::seconds(10);
    std::string m_wallet_file;
    std::string m_payment_id;
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
    const command_line::arg_descriptor<std::string> arg_supernode_address = { "supernode-address", "Supernode address", "localhost:28900", false };
    const command_line::arg_descriptor<std::string> arg_wallet_file = { "wallet-file", "Wallet file", "wallet", false };


    command_line::add_arg(desc_cmd, arg_input_file);
    command_line::add_arg(desc_cmd, arg_log_level);
    command_line::add_arg(desc_cmd, arg_amount);

    command_line::add_arg(desc_cmd, arg_sale_items_file);
    command_line::add_arg(desc_cmd, arg_sale_timeout);
    command_line::add_arg(desc_cmd, arg_supernode_address);
    command_line::add_arg(desc_cmd, arg_wallet_file);
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
    std::cout << "pos-wallet-address: " << command_line::get_arg(vm, arg_wallet_file) << std::endl;

    uint64_t amount = 0;
    std::string amount_str = command_line::get_arg(vm, arg_amount);
    if (!cryptonote::parse_amount(amount, amount_str)) {
        MERROR("Failed to parse amount : " << amount_str);
        return 1;
    }

    Wallet wallet(command_line::get_arg(vm, arg_input_file),
            command_line::get_arg(vm, arg_supernode_address),
            command_line::get_arg(vm, arg_sale_timeout),
            command_line::get_arg(vm, arg_wallet_file)
            );

    if (!wallet.readPaymentDetails()) {
        MERROR("Failed to read payment from QR code");
        return EXIT_FAILURE;
    }

    if (!wallet.pay()) {
        MERROR("Failed to read payment from QR code");
        return EXIT_FAILURE;
    }

    // TODO: monitor for a payment status

    return 0;

    CATCH_ENTRY("App error", 1);
}
