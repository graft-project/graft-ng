#include <gtest/gtest.h>
#include "lib/graft/jsonrpc.h"
#include "fixture.h"

class UpstreamTest : public GraftServerTestBase
{
public:
    MainServer m_mainServer;

    void run(const std::string& forwardUri, const int maxConn)
    {
        auto action = [&](const graft::Router::vars_t& vars, const graft::Input& input, graft::Context& ctx, graft::Output& output)->graft::Status
        {
            switch(ctx.local.getLastStatus())
            {
            case graft::Status::None :
            {
                output.body = input.body;
                output.uri = forwardUri;
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

        std::atomic<int> activeCnt = 0;
        std::atomic<int> maxActiveCnt = 0;

        std::mutex mutex;

        std::map<mg_connection*, std::string> results;

        TempCryptoNodeServer crypton;
        crypton.on_http = [&maxActiveCnt, &activeCnt, &mutex, &results] (mg_connection* client, const http_message *hm, int& status_code, std::string& headers, std::string& data) -> bool
        {
            if(maxActiveCnt < ++activeCnt) maxActiveCnt = activeCnt.load();
            std::string body = std::string(hm->body.p, hm->body.len);

            std::thread th {[=, &mutex, &results]()
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                std::lock_guard<std::mutex> lock(mutex);
                results.emplace(std::make_pair(client, body));
            }};
            th.detach();
            return false;
        };
        crypton.on_poll = [&activeCnt, &mutex, &results] (mg_connection* client, int& status_code, std::string& headers, std::string& data)->bool
        {
            std::lock_guard<std::mutex> lock(mutex);
            auto it = results.find(client);
            if(it == results.end()) return false;
            headers = "Content-Type: application/json";
            data = it->second;
            results.erase(it);
            --activeCnt;
            return true;
        };
        crypton.keepAlive = true;
        crypton.poll_timeout_ms = 100;
        crypton.connect_timeout_ms = 1000;
        crypton.run();

        m_mainServer.m_router.addRoute("/test_upstream", METHOD_POST, {nullptr, action, nullptr});
        m_mainServer.run();

        std::atomic<int> ok_cnt = 0;

        auto client_func = [&ok_cnt](int i)
        {
            std::string post_data = "some data" + std::to_string(i);
            Client client;
            client.serve("http://localhost:9084/test_upstream", "", post_data, 100, 5000);
            EXPECT_EQ(false, client.get_closed());
            EXPECT_EQ(200, client.get_resp_code());
            std::string s = client.get_body();
            EXPECT_EQ(s, post_data);
            if(s == post_data) ++ok_cnt;
        };

        const int th_cnt = 50;
        std::vector<std::thread> th_vec;
        for(int i = 0; i < th_cnt; ++i) th_vec.emplace_back(std::thread([i, client_func](){ client_func(i); }));
        for(auto& th : th_vec) th.join();

        if(maxConn != 0) EXPECT_EQ(maxActiveCnt, maxConn);

        EXPECT_EQ(ok_cnt, th_cnt);

        m_mainServer.stop_and_wait_for();
        crypton.stop_and_wait_for();
    }
};

TEST_F(UpstreamTest, upstream)
{
    const int maxConn = 0;
    m_mainServer.m_copts.uri_substitutions.insert({"crypton", {"127.0.0.1:1234", maxConn, false, 300}});
    run("$crypton", maxConn);
}

TEST_F(UpstreamTest, upstreamWithSharp)
{
    const int maxConn = 0;
    m_mainServer.m_copts.uri_substitutions.insert({"crypton", {"127.0.0.1:1234", maxConn, false, 300}});
    m_mainServer.m_copts.graftlet_dirs.emplace_back("graftlets");
    run("#myGraftlet.testGL.testRouting:$crypton", maxConn);
}

TEST_F(UpstreamTest, upstreamDefault)
{
    const int maxConn = 0;
    m_mainServer.m_copts.cryptonode_rpc_address = "";
    m_mainServer.m_copts.default_uri_substitution_name = "default";
    m_mainServer.m_copts.uri_substitutions.insert({"default", {"127.0.0.1:1234", maxConn, false, 300}});
    run("", maxConn);
}

TEST_F(UpstreamTest, upstreamKeepAlive)
{
    const int maxConn = 0;
    m_mainServer.m_copts.uri_substitutions.insert({"crypton", {"127.0.0.1:1234", maxConn, true, 300}});
    run("$crypton", maxConn);
}

TEST_F(UpstreamTest, upstreamKeepAliveDefault)
{
    const int maxConn = 0;
    m_mainServer.m_copts.cryptonode_rpc_address = "";
    m_mainServer.m_copts.default_uri_substitution_name = "default";
    m_mainServer.m_copts.uri_substitutions.insert({"default", {"127.0.0.1:1234", maxConn, true, 300}});
    run("", maxConn);
}

TEST_F(UpstreamTest, upstreamMax)
{
    const int maxConn = 3;
    m_mainServer.m_copts.uri_substitutions.insert({"crypton", {"127.0.0.1:1234", maxConn, false, 1000}});
    run("$crypton", maxConn);
}

TEST_F(UpstreamTest, upstreamMaxDefault)
{
    const int maxConn = 3;
    m_mainServer.m_copts.cryptonode_rpc_address = "";
    m_mainServer.m_copts.default_uri_substitution_name = "default";
    m_mainServer.m_copts.uri_substitutions.insert({"default", {"127.0.0.1:1234", maxConn, false, 1000}});
    run("", maxConn);
}

TEST_F(UpstreamTest, upstreamMaxKeepAlive)
{
    const int maxConn = 3;
    m_mainServer.m_copts.uri_substitutions.insert({"crypton", {"127.0.0.1:1234", maxConn, true, 1000}});
    run("$crypton", maxConn);
}

TEST_F(UpstreamTest, upstreamMaxKeepAliveDefault)
{
    const int maxConn = 3;
    m_mainServer.m_copts.cryptonode_rpc_address = "";
    m_mainServer.m_copts.default_uri_substitution_name = "default";
    m_mainServer.m_copts.uri_substitutions.insert({"default", {"127.0.0.1:1234", maxConn, true, 1000}});
    run("", maxConn);
}
