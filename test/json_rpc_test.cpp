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


#include <gtest/gtest.h>
#include "lib/graft/inout.h"
#include "lib/graft/jsonrpc.h"
#include "lib/graft/context.h"
#include "lib/graft/sys_info.h"
#include "fixture.h"
#include "misc_log_ex.h"
#include <string>
#include <thread>
#include <chrono>

using namespace graft;
using namespace std::chrono_literals;

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


GRAFT_DEFINE_IO_STRUCT_INITED(TestRequestParam,
                              (bool, return_success, true));

GRAFT_DEFINE_IO_STRUCT_INITED(TestResponseParam,
                              (std::string, foo, ""),
                              (int, bar, 0),
                              (std::vector<int>, baz, std::vector<int>())
                              );

GRAFT_DEFINE_JSON_RPC_REQUEST(JsonRpcTestRequest, TestRequestParam);
GRAFT_DEFINE_JSON_RPC_RESPONSE(JsonRpcTestResponse, TestResponseParam);
GRAFT_DEFINE_JSON_RPC_RESPONSE_RESULT(JsonRpcTestResponseResult, TestResponseParam);

TEST(JsonRPCFormat, request_parse)
{
    std::string json_rpc_req = "{\"json\":\"2.0\",\"id\":\"0\",\"method\":\"Hello\",\"params\": {\"return_success\" : \"false\"} }";
    Input in; in.load(json_rpc_req);
    JsonRpcTestRequest req = in.get<JsonRpcTestRequest>();

}

TEST(JsonRPCFormat, malformed_source)
{
    std::string json_rpc_req = "{\"json\":\"2.0\", +,;\"id\":\"0\",\"method\",:\"Hello\",\"params\": {\"return_success\" : \"false\"} }";
    Input in; in.load(json_rpc_req);
    JsonRpcTestRequest req;
    EXPECT_ANY_THROW(in.get<JsonRpcTestRequest>());
    EXPECT_FALSE(in.get(req));
}

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



struct JsonRpcTest : public ::testing::Test
{

    static Status jsonRpcHandler(const Router::vars_t& vars, const graft::Input& input,
                                     graft::Context& ctx, graft::Output& output)
    {


        JsonRpcTestRequest request = input.get<JsonRpcTestRequest>();
        // success response;
        if (request.params.return_success) {
            LOG_PRINT_L0("Returning 'result' response...");
            JsonRpcTestResponseResult response;
            response.id = request.id;
            response.result.foo = "Hello";
            response.result.bar = 1;
            for (int i = 0; i < 10; ++i)
                response.result.baz.push_back(i);

            //std::this_thread::sleep_for(10s);

            output.load(response);
            return Status::Ok;
        // error response
        } else {
            LOG_PRINT_L0("Returning 'error' response...");
            JsonRpcErrorResponse response;
            response.error.message  = "Something wrong";
            response.error.code  = -1;
            response.id = request.id;
            output.load(response);
            return Status::Error;
        }
    }

    void startServer()
    {
        ConfigOpts sopts {"", "localhost:8855", "localhost:8856", 5.0, 5.0, 0, 0, 1000, "localhost:28281/sendrawtransaction", 1000, -1, {}, 60000};
        Router router;
        Router::Handler3 h3(nullptr, jsonRpcHandler, nullptr);
        router.addRoute("/jsonrpc/test", METHOD_POST, h3);

        detail::GSTest gserver(router, true);
        gserver.init(start_args.argc, start_args.argv, sopts);
        this->m_gserver = &gserver;
        gserver.run();
    }

    void stopServer()
    {

    }

    JsonRpcTest()
    {
        mlog_configure("", true);
        mlog_set_log_level(1);

        m_serverThread = std::thread([this]() {
            this->startServer();
        });
        LOG_PRINT_L0("Server thread started..");
        while (!m_gserver || !m_gserver.load()->ready()) {
            LOG_PRINT_L0("waiting for server");
            std::this_thread::sleep_for(1s);
        }
        LOG_PRINT_L0("Server ready");
    }

    ~JsonRpcTest()
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
        LOG_PRINT_L0("curl invocation: " << ss);
        //return "";
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

    std::atomic<detail::GSTest*> m_gserver{nullptr};
    std::thread m_serverThread;
};


TEST_F(JsonRpcTest, common)
{
    JsonRpcTestRequest request;
    request.id = 0;
    TestRequestParam request_param; request_param.return_success = true;
    initJsonRpcRequest(request, 1, "Hello", request_param);
    LOG_PRINT_L0("Sending request...");
    std::string response_s = send_request("http://localhost:8855/jsonrpc/test", escape_string_curl(request.toJson().GetString()));
    LOG_PRINT_L0("response: " << response_s);

    Input in; in.load(response_s);
    JsonRpcTestResponseResult response_result = in.get<JsonRpcTestResponseResult>();

    EXPECT_TRUE(response_result.id == 1);
    EXPECT_TRUE(response_result.result.foo == "Hello");
    EXPECT_TRUE(response_result.result.bar == 1);
    EXPECT_TRUE(response_result.result.baz.size() == 10);


    request.params.return_success = false;
    LOG_PRINT_L0("Sending request...");
    response_s = send_request("http://localhost:8855/jsonrpc/test", escape_string_curl(request.toJson().GetString()));
    LOG_PRINT_L0("response: " << response_s);
    in.load(response_s);
    JsonRpcErrorResponse response_error = in.get<JsonRpcErrorResponse>();
    EXPECT_TRUE(response_error.id == 1);
    EXPECT_TRUE(response_error.error.code == -1);

    LOG_PRINT_L0("Stopping server..");
    m_gserver.load()->stop();
    LOG_PRINT_L0("Waiting for a server thread done...");
    m_serverThread.join();
    LOG_PRINT_L0("Server thread done...");
}


