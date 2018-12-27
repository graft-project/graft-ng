#include <gtest/gtest.h>
#include "lib/graft/jsonrpc.h"
#include "fixture.h"

TEST_F(GraftServerTestBase, upstreamKeepAlive)
{
    auto action = [&](const graft::Router::vars_t& vars, const graft::Input& input, graft::Context& ctx, graft::Output& output)->graft::Status
    {
        switch(ctx.local.getLastStatus())
        {
        case graft::Status::None :
        {
            output.body = input.body;
            output.uri = "$crypton";
            return graft::Status::Forward;
        } break;
        case graft::Status::Forward :
        {
            output.body = input.body;
            return graft::Status::Ok;
        } break;
        default: assert(false);
        }
    };

    TempCryptoNodeServer crypton;
    crypton.on_http = [] (const http_message *hm, int& status_code, std::string& headers, std::string& data) -> bool
    {
        data = std::string(hm->body.p, hm->body.len);
        std::string method(hm->method.p, hm->method.len);
        headers = "Content-Type: application/json";
        return true;
    };
    crypton.keepAlive = true;
    crypton.connect_timeout_ms = 1000;
    crypton.run();
    graft::Output::uri_substitutions.insert({"crypton", {"127.0.0.1:1234", 3, true, 100}});
    MainServer mainServer;
    mainServer.m_router.addRoute("/test_upstream", METHOD_POST, {nullptr, action, nullptr});
    mainServer.run();

    auto client_func = [](int i)
    {
        std::string post_data = "some data" + std::to_string(i);
        Client client;
        client.serve("http://localhost:9084/test_upstream", "", post_data, 1000, 250);
        EXPECT_EQ(false, client.get_closed());
        EXPECT_EQ(200, client.get_resp_code());
        std::string s = client.get_body();
        EXPECT_EQ(s, post_data);
    };

    for(int c = 0; c < 1; ++c)
    {
        const int th_cnt = 50;
        std::vector<std::thread> th_vec;
        for(int i = 0; i < th_cnt; ++i) th_vec.emplace_back(std::thread([i, client_func](){ client_func(i); }));
        for(auto& th : th_vec) th.join();
    }

    mainServer.stop_and_wait_for();
    crypton.stop_and_wait_for();
}


namespace
{

GRAFT_DEFINE_IO_STRUCT(GetVersionResp,
                       (std::string, status),
                       (uint32_t, version)
                       );

GRAFT_DEFINE_JSON_RPC_RESPONSE_RESULT(JRResponseResult, GetVersionResp);

} //namespace

//this test requires a cryptonode on 127.0.0.1:18981
TEST_F(GraftServerTestBase, DISABLED_cryptonodeKeepAlive)
{
    auto action = [&](const graft::Router::vars_t& vars, const graft::Input& input, graft::Context& ctx, graft::Output& output)->graft::Status
    {
        switch(ctx.local.getLastStatus())
        {
        case graft::Status::None :
        {
            output.uri = "$cryptonode";
            graft::JsonRpcRequestHeader request;
            request.method = "get_version";
            output.load(request);
            return graft::Status::Forward;
        } break;
        case graft::Status::Forward :
        {
            output.body = input.body;
            return graft::Status::Ok;
        } break;
        default: assert(false);
        }
    };

    graft::Output::uri_substitutions.insert({"cryptonode", {"127.0.0.1:18981/json_rpc", 3, true, 100}});
    MainServer mainServer;
    mainServer.m_router.addRoute("/test_upstream", METHOD_POST, {nullptr, action, nullptr});
    mainServer.run();

    auto client_func = [](int i)
    {
        std::string post_data = "some data" + std::to_string(i);
        Client client;
        client.serve("http://localhost:9084/test_upstream", "", post_data, 1000, 250);
        EXPECT_EQ(false, client.get_closed());
        EXPECT_EQ(200, client.get_resp_code());
        std::string s = client.get_body();

        graft::Input in; in.load(client.get_body());
        EXPECT_NO_THROW( JRResponseResult result = in.get<JRResponseResult>() );
    };

    for(int c = 0; c < 1; ++c)
    {
        const int th_cnt = 50;
        std::vector<std::thread> th_vec;
        for(int i = 0; i < th_cnt; ++i) th_vec.emplace_back(std::thread([i, client_func](){ client_func(i); }));
        for(auto& th : th_vec) th.join();
    }

    mainServer.stop_and_wait_for();
}
