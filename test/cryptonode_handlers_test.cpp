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


#include <misc_log_ex.h>
#include <gtest/gtest.h>
#include <inout.h>
#include <jsonrpc.h>
#include <connection.h>
#include <router.h>
#include <requests.h>
#include <requests/getinforequest.h>
#include <requests/sendrawtxrequest.h>
#include <requests/authorizertatxrequest.h>
//#include <requests/sendtxauthresponserequest.h>


// cryptonode includes
#include <wallet/wallet2.h>


#include <string>
#include <thread>
#include <chrono>



using namespace graft;
using namespace std::chrono_literals;

struct CryptonodeHandlersTest : public ::testing::Test
{
    // this     is blocking function that will not end until server stopped explicitly
    void startServer()
    {
        httpcm->bind(*looper);
        looper->serve();
    }

    CryptonodeHandlersTest()
    {
        mlog_configure("", true);
        mlog_set_log_level(2);
        LOG_PRINT_L0("L0");
        LOG_PRINT_L1("L1");
        LOG_PRINT_L2("L2");

        ConfigOpts copts {"localhost:8855", "localhost:8856", 5.0, 5.0, 0, 0, "localhost:28281/sendrawtransaction", 1000};
        looper = std::make_unique<Looper>(copts);

        Router router;

        graft::registerGetInfoRequest(router);
        graft::registerSendRawTxRequest(router);
        graft::registerAuthorizeRtaTxRequests(router);

        httpcm = std::make_unique<HttpConnectionManager>();
        httpcm->addRouter(router);
        httpcm->enableRouting();

        server_thread = std::thread([this]() {
            this->startServer();
        });

        LOG_PRINT_L0("Server thread started..");

        while (!looper->ready()) {
            LOG_PRINT_L0("waiting for server");
            std::this_thread::sleep_for(1s);
        }

        LOG_PRINT_L0("Server ready");
    }

    ~CryptonodeHandlersTest()
    {

    }


    static std::string run_cmdline_read(const std::string& cmdl)
    {
        FILE* fp = popen(cmdl.c_str(), "r");
        assert(fp);
        std::string res;
        char path[1035];
        while(fgets(path, sizeof(path)-1, fp))
        {
            res += path;
        }
        pclose(fp);
        return res;
    }
    static std::string escape_string_curl(const std::string & in)
    {
        std::string result;
        for (char c : in) {
            if (c == '"')
                result.push_back('\\');
            result.push_back(c);
        }
        return result;
    }

    static std::string send_request(const std::string &url, const std::string &json_data)
    {
        std::ostringstream s;
        s << "curl -s -H \"Content-type: application/json\" --data \"" << json_data << "\" " << url;
        std::string ss = s.str();
        return run_cmdline_read(ss.c_str());
    }

    static std::string send_request(const std::string &url)
    {
        std::ostringstream s;
        s << "curl -s " << url;
        std::string ss = s.str();
        return run_cmdline_read(ss.c_str());
    }


    static void SetUpTestCase()
    {

    }

    static void TearDownTestCase()
    {
    }

    virtual void SetUp() override
    { }
    virtual void TearDown() override
    { }


    std::unique_ptr<HttpConnectionManager> httpcm;
    std::unique_ptr<Looper>     looper;

    std::thread server_thread;
};



TEST_F(CryptonodeHandlersTest, getinfo)
{
    MGINFO_YELLOW("*** This test requires running cryptonode RPC on localhost:28881. If not running, test will fail ***");


    std::string response_s = send_request("http://localhost:8855/cryptonode/getinfo");
    EXPECT_FALSE(response_s.empty());
    server_thread.join();
    return;

    LOG_PRINT_L2("response: " << response_s);
    Input in; in.load(response_s);
    GetInfoResponse resp = in.get<GetInfoResponse>();
    EXPECT_TRUE(resp.height > 0);
    EXPECT_TRUE(resp.status == "OK");
    EXPECT_TRUE(resp.difficulty > 0);
    EXPECT_TRUE(resp.tx_count > 0);
    LOG_PRINT_L2("Stopping server...");
    looper->stop();
    server_thread.join();
    LOG_PRINT_L2("Server stopped, Server thread done...");
}

struct FooBar
{
    std::string foo;
    int bar;
};

TEST_F(CryptonodeHandlersTest, sendrawtx)
{
    MGINFO_YELLOW("*** This test requires running cryptonode with public testnet blockchain running on localhost:28881. If not running, test will fail ***");

    FooBar fb {"1", 2};

    // open wallet
    const std::string wallet_path = "test_wallet";
    tools::wallet2 wallet(true);

    wallet.load(wallet_path, "");
    wallet.init("localhost:28881");
    wallet.refresh();
    wallet.store();
    std::cout << "wallet addr: " <<  wallet.get_account().get_public_address_str(true) << std::endl;

    const uint64_t AMOUNT_1_GRFT = 10000000000000;
    // send to itself
    cryptonote::tx_destination_entry de (AMOUNT_1_GRFT, wallet.get_account().get_keys().m_account_address);
    std::vector<cryptonote::tx_destination_entry> dsts; dsts.push_back(de);
    std::vector<uint8_t> extra;
    std::vector<tools::wallet2::pending_tx> ptx = wallet.create_transactions_2(dsts, 4, 0, 0, extra, true);
    ASSERT_TRUE(ptx.size() == 1);
    SendRawTxRequest req;
    ASSERT_TRUE(createSendRawTxRequest(ptx.at(0), req));
    std::string request_s = req.toJson().GetString();
    LOG_PRINT_L2("sending to supernode: " << request_s);
    std::string response_s = send_request("http://localhost:8855/cryptonode/sendrawtx",
                                          escape_string_curl(request_s));
    EXPECT_FALSE(response_s.empty());
    LOG_PRINT_L2("response: " << response_s);
    Input in; in.load(response_s);
    SendRawTxResponse  resp = in.get<SendRawTxResponse>();
    wallet.store();
    EXPECT_TRUE(resp.status == "OK");
    EXPECT_FALSE(resp.not_relayed);
    EXPECT_FALSE(resp.low_mixin);
    EXPECT_FALSE(resp.double_spend);
    EXPECT_FALSE(resp.invalid_input);
    EXPECT_FALSE(resp.invalid_output);
    EXPECT_FALSE(resp.too_big);
    EXPECT_FALSE(resp.overspend);
    EXPECT_FALSE(resp.fee_too_low);
    EXPECT_FALSE(resp.not_rct);

    LOG_PRINT_L2("Stopping server...");
    looper->stop();
    server_thread.join();
    LOG_PRINT_L2("Server stopped, Server thread done...");
}


