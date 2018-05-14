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
#include <graft_manager.h>
#include <router.h>

#include <string>
#include <thread>


using namespace graft;

GRAFT_DEFINE_IO_STRUCT_INITED(Payment1,
     (uint64, amount, 0),
     (uint32, block_height, 0),
     (std::string, payment_id, ""),
     (std::string, tx_hash, ""),
     (uint32, unlock_time, 0)
 );


GRAFT_DEFINE_IO_STRUCT(Payments,
                       (std::vector<Payment1>, payments)
);




GRAFT_DEFINE_JSON_RPC_REQUEST(JsonRPCRequest, Payment1);
GRAFT_DEFINE_JSON_RPC_RESPONSE(JsonRPCResponse, Payment1);
GRAFT_DEFINE_JSON_RPC_RESPONSE_RESULT(JsonRPCResponseResult, Payment1);


TEST(JsonRPCFormat, error_and_result_parse)
{
    using namespace graft;
    std::string json_rpc_response  = " {\"json\":\"\",\"id\":3355185,\"result\":{\"amount\":0,\"block_height\":3581286912,\"payment_id\":\"\",\"tx_hash\":\"\",\"unlock_time\":1217885840}}";

    Input in_result; in_result.load(json_rpc_response);

    JsonRPCResponse response1;
    // testing 'result' response
    ASSERT_NO_THROW(response1 = in_result.get<JsonRPCResponse>());

    EXPECT_TRUE(response1.error.code == 0);

    JsonRPCResponseResult result = in_result.get<JsonRPCResponseResult>();
    EXPECT_TRUE(result.result.block_height == 3581286912);

    // testing 'error' response
    std::string json_rpc_error     = " {\"json\":\"\",\"id\":3355185,\"error\":{\"code\":123,\"message\":\"Error Message\"}}";
    Input in_err; in_err.load(json_rpc_error);
    JsonRPCResponse response2;
    ASSERT_NO_THROW(response2 = in_err.get<JsonRPCResponse>());
    EXPECT_TRUE(response2.error.code == 123);
    EXPECT_TRUE(response2.result.amount == 0);
    EXPECT_TRUE(response2.result.block_height == 0);
}

GRAFT_DEFINE_IO_STRUCT_INITED(TestRequestParam,
                              (bool, return_success, true));

GRAFT_DEFINE_IO_STRUCT_INITED(TestResponseParam,
                              (std::string, foo, ""),
                              (int, bar, 0),
                              (std::vector<int>, baz, std::vector<int>())
                              );

GRAFT_DEFINE_JSON_RPC_REQUEST(JsonRpcTestRequest, TestRequestParam);
GRAFT_DEFINE_JSON_RPC_RESPONSE(JsonRpcTestResponse, TestRequestParam);
GRAFT_DEFINE_JSON_RPC_RESPONSE_RESULT(JsonRpcTestResponseResult, TestRequestParam);

struct JsonRpcTest : public ::testing::Test
{

    static Router::Status jsonRpcHandler(const Router::vars_t& vars, const graft::Input& input,
                                     graft::Context& ctx, graft::Output& output)
    {

    }

    JsonRpcTest()
    {
        mlog_configure("", true);
        ServerOpts sopts {"localhost:8855", 5.0, 4, 4};
        Router router;
        Router::Handler3 h3(nullptr, jsonRpcHandler, nullptr);
        router.addRoute("/jsonrpc/test", METHOD_POST, h3);
        router.arm();
        manager = new Manager(router, sopts);
        server_thread = std::thread([&]() {
            server.serve(manager->get_mg_mgr());
        });
    }

    ~JsonRpcTest()
    {
        delete manager;
        server_thread.join();
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

    static std::string send_request(const std::string &url, const std::string &json_data)
    {
        std::ostringstream s;
        s << "curl --data \"" << json_data << "\" " << url;
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

    GraftServer server;
    Manager   * manager;
    std::thread server_thread;
};


TEST_F(JsonRpcTest, resultResponse)
{
    LOG_PRINT_L1("resultResponse");
}


TEST_F(JsonRpcTest, errorResponse)
{
    LOG_PRINT_L2("errorResponse");
}
