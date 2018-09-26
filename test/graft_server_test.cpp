#include <gtest/gtest.h>
#include <deque>
#include <jsonrpc.h>
#include <boost/uuid/uuid_io.hpp>
#include <misc_log_ex.h>

#include "context.h"
#include "requests.h"
#include "salerequest.h"
#include "salestatusrequest.h"
#include "rejectsalerequest.h"
#include "saledetailsrequest.h"
#include "payrequest.h"
#include "paystatusrequest.h"
#include "rejectpayrequest.h"
#include "requestdefines.h"
#include "inout.h"
#include "fixture.h"

GRAFT_DEFINE_IO_STRUCT(Payment,
      (uint64, amount),
      (uint32, block_height),
      (std::string, payment_id),
      (std::string, tx_hash),
      (uint32, unlock_time)
);

GRAFT_DEFINE_IO_STRUCT(Sstr,
      (std::string, s)
);

TEST(InOut, common)
{
    using namespace graft;

    auto f = [](const Input& in, Output& out)
    {
        Payment p = in.get<Payment>();
        EXPECT_EQ(p.amount, 10350000000000);
        EXPECT_EQ(p.block_height, 994327);
        EXPECT_EQ(p.payment_id, "4279257e0a20608e25dba8744949c9e1caff4fcdafc7d5362ecf14225f3d9030");
        EXPECT_EQ(p.tx_hash, "c391089f5b1b02067acc15294e3629a463412af1f1ed0f354113dd4467e4f6c1");
        EXPECT_EQ(p.unlock_time, 0);
        ++p.amount;
        ++p.block_height;
        p.payment_id = "4fcdafc7d5362ecf14225f3d9030";
        p.tx_hash = "acc15294e3629a463412af1f1ed0f";
        ++p.unlock_time;
        out.load(p);
    };

    std::string s_in = "\
    {\
        \"amount\": 10350000000000,\
        \"block_height\": 994327,\
        \"payment_id\": \"4279257e0a20608e25dba8744949c9e1caff4fcdafc7d5362ecf14225f3d9030\",\
        \"tx_hash\": \"c391089f5b1b02067acc15294e3629a463412af1f1ed0f354113dd4467e4f6c1\",\
        \"unlock_time\": 0\
    }";
    Input in;
    in.load(s_in.c_str(), s_in.size());
    in.load(s_in.c_str(), s_in.size());
    Output out;
    f(in, out);
    auto pair = out.get();
    std::string s_out(pair.first, pair.second);
    std::string s = "\
    {\
        \"amount\": 10350000000001,\
        \"block_height\": 994328,\
        \"payment_id\": \"4fcdafc7d5362ecf14225f3d9030\",\
        \"tx_hash\": \"acc15294e3629a463412af1f1ed0f\",\
        \"unlock_time\": 1\
    }";
    //remove all spaces
    s.erase(std::remove_if(s.begin(), s.end(), isspace), s.end());
    EXPECT_EQ(s_out, s);
}

namespace graft { namespace serializer {

template<typename T>
struct Nothing
{
    static std::string serialize(const T& t)
    {
        return "";
    }
    static void deserialize(const std::string& s, T& t)
    {
    }
};

} }

TEST(InOut, serialization)
{
    using namespace graft;

    GRAFT_DEFINE_IO_STRUCT(J,
        (int,x),
        (int,y)
    );

    J j;

    Input input;
    input.body = "{\"x\":1,\"y\":2}";
        j.x = 5; j.y = 6;
    j = input.get<J, serializer::JSON<J>>();
        EXPECT_EQ(j.x, 1); EXPECT_EQ(j.y, 2);
    j = input.get<J>();
        EXPECT_EQ(j.x, 1); EXPECT_EQ(j.y, 2);
        j.x = 5; j.y = 6;
    j = input.getT<serializer::JSON, J>();
        EXPECT_EQ(j.x, 1); EXPECT_EQ(j.y, 2);

    Output output;
    output.load<J, serializer::JSON<J>>(j);
    output.load<J>(j);
    output.load<>(j);
        EXPECT_EQ(input.body, output.body);
        output.body.clear();
    output.load(j);
        EXPECT_EQ(input.body, output.body);
    output.body.clear();
    output.loadT<serializer::JSON, J>(j);
    output.loadT<serializer::JSON>(j);
    output.loadT<>(j);
        EXPECT_EQ(input.body, output.body);
        output.body.clear();
    output.loadT(j);
        EXPECT_EQ(input.body, output.body);

    struct A
    {
        int x;
        int y;
    };

    A a;

    a = input.get<A, serializer::Nothing<A>>();
    a = input.getT<serializer::Nothing, A>();
    output.load<A, serializer::Nothing<A>>(a);
    output.loadT<serializer::Nothing, A>(a);
    output.loadT<serializer::Nothing>(a);
}

TEST(InOut, makeUri)
{
    {
        graft::Output output;
        std::string default_uri = "http://123.123.123.123:1234";
        std::string url = output.makeUri(default_uri);
        EXPECT_EQ(url, default_uri);
    }
    {
        graft::Output output;
        graft::Output::uri_substitutions.insert({"my_ip", "1.2.3.4"});
        output.proto = "https";
        output.port = "4321";
        output.uri = "$my_ip";
        std::string url = output.makeUri("");
        EXPECT_EQ(url, output.proto + "://1.2.3.4:" + output.port);
    }
    {
        graft::Output output;
        graft::Output::uri_substitutions.insert({"my_path", "http://site.com:1234/endpoint?q=1&n=2"});
        output.proto = "https";
        output.port = "4321";
        output.uri = "$my_path";
        std::string url = output.makeUri("");
        EXPECT_EQ(url, "https://site.com:4321/endpoint?q=1&n=2");
    }
    {
        graft::Output output;
        graft::Output::uri_substitutions.insert({"my_path", "endpoint?q=1&n=2"});
        output.proto = "https";
        output.host = "mysite.com";
        output.port = "4321";
        output.uri = "$my_path";
        std::string url = output.makeUri("");
        EXPECT_EQ(url, "https://mysite.com:4321/endpoint?q=1&n=2");
    }
    {
        graft::Output output;
        std::string default_uri = "localhost:28881";
        output.path = "json_rpc";
        std::string url = output.makeUri(default_uri);
        EXPECT_EQ(url, "localhost:28881/json_rpc");

        output.path = "/json_rpc";
        output.proto = "https";
        url = output.makeUri(default_uri);
        EXPECT_EQ(url, "https://localhost:28881/json_rpc");

        output.path = "/json_rpc";
        output.proto = "https";
        output.uri = "http://aaa.bbb:12345/something";
        url = output.makeUri(default_uri);
        EXPECT_EQ(url, "https://aaa.bbb:12345/json_rpc");
    }
}

TEST(Context, simple)
{
    graft::GlobalContextMap m;
    graft::Context ctx(m);

    std::string key[] = { "aaa", "aa" }, s = "aaaaaaaaaaaa";
    std::vector<char> v({'1','2','3'});
#if 0
    ctx.local[key[0]] = s;
    ctx.local[key[1]] = v;
    std::string bb = ctx.local[key[0]];
    const std::vector<char> cc = ctx.local[key[1]];
    //    std::string& c = ctx.local[key];

    //    std::cout << bb << " " << c << std::endl;
    //    std::cout << c << std::endl;
    ctx.local[key[0]] = std::string("bbbbb");

    const std::string& b = ctx.local[key[0]];
    std::cout << b << " " << bb << " " << std::string(cc.begin(), cc.end()) << std::endl;
#endif
    ctx.global[key[0]] = s;
    ctx.global[key[1]] = v;
    std::string bbg = ctx.global[key[0]];
    const std::vector<char> ccg = ctx.global[key[1]];
    //    std::string& c = ctx.global[key];

    //    std::cout << bb << " " << c << std::endl;
    //    std::cout << c << std::endl;
    std::string s1("bbbbb");
    ctx.global[key[0]] = s1;

    std::string bg = ctx.global[key[0]];
    EXPECT_EQ( bg, s1 );
    EXPECT_EQ( bbg, s );
    EXPECT_EQ( ccg, std::vector<char>({'1', '2', '3'}) );
    {//test apply
        ctx.global["x"] = 1;
        std::function<bool(int&)> f = [](int& v)->bool { ++v; return true; };
        ctx.global.apply("x", f);
        int t = ctx.global["x"];
        EXPECT_EQ( t, 2);
    }
#if 0
    ctx.put("aaa", std::string("aaaaaaaaaaaa"));
    ctx.put("bbb", 5);

    std::string s;
    int i;

    bool b = ctx.take("aaa", s) && ctx.take("bbb", i);
    EXPECT_EQ(b, true);
    ASSERT_STREQ(s.c_str(), "aaaaaaaaaaaa");
    EXPECT_EQ(i, 5);

    ctx.put("aaa", std::string("aaaaaaaaaaaa"));
    ctx.purge();
    b = ctx.take("aaa", s);
    EXPECT_EQ(b, false);
#endif
}

TEST(Context, stress)
{
    graft::GlobalContextMap m;
    graft::Context ctx(m);
    std::vector<std::string> v_keys;
    {
        const char keys[] = "abcdefghijklmnopqrstuvwxyz";
        const int keys_cnt = sizeof(keys)/sizeof(keys[0]);
        for(int i = 0; i< keys_cnt; ++i)
        {
            char ch1 = keys[(5*i)%keys_cnt], ch2 = keys[(5*i+7)%keys_cnt];
            std::string key(1, ch1); key += ch2;
            v_keys.push_back(key);
            ctx.global[key] = key;
            ctx.local[key] = key;
        }
    }
    std::for_each(v_keys.rbegin(), v_keys.rend(), [&](auto& key)
    {
        std::string sg = ctx.global[key];
        std::string sl = ctx.local[key];
        EXPECT_EQ( sg, key );
        EXPECT_EQ( sl, key );
    });
    for(int i = 0; i < v_keys.size(); i += 2)
    {
        auto it = v_keys.begin() + i;
        std::string& key = *it;
        ctx.global.remove(key);
        ctx.local.remove(key);
        EXPECT_EQ( false, ctx.global.hasKey(key) );
        EXPECT_EQ( false, ctx.local.hasKey(key) );
        v_keys.erase(it);
    }
    std::for_each(v_keys.begin(), v_keys.end(), [&](auto& key)
    {
        std::string sg = ctx.global[key];
        std::string sl = ctx.local[key];
        EXPECT_EQ( sg, key );
        EXPECT_EQ( sl, key );
    });
}

TEST(Context, multithreaded)
{
    graft::GlobalContextMap m;
    std::vector<std::string> v_keys;
    {//init global
        graft::Context ctx(m);
        const char keys[] = "abcdefghijklmnopqrstuvwxyz";
        const int keys_cnt = sizeof(keys)/sizeof(keys[0]);
        for(int i = 0; i< keys_cnt; ++i)
        {
            char ch1 = keys[(5*i)%keys_cnt], ch2 = keys[(5*i+7)%keys_cnt];
            std::string key(1, ch1); key += ch2;
            v_keys.push_back(key);
            ctx.global[key] = (uint64_t)0;
        }
    }

    std::atomic<uint64_t> g_count(0);
    std::function<bool(uint64_t&)> f = [](uint64_t& v)->bool { ++v; return true; };
    int pass_cnt;
    {//get pass count so that overall will take about 1000 ms
        graft::Context ctx(m);
        auto begin = std::chrono::high_resolution_clock::now();
        std::for_each(v_keys.begin(), v_keys.end(), [&] (auto& key)
        {
            ctx.global.apply(key, f);
            ++g_count;
        });
        auto end = std::chrono::high_resolution_clock::now();
        std::chrono::high_resolution_clock::duration d = std::chrono::milliseconds(1000);
        pass_cnt = d.count() / (end - begin).count();
    }

    EXPECT_LE(2, pass_cnt);

    const int th_count = 4;

    //forward and backward passes
    std::atomic_int stop(0);

    auto f_f = [&] ()
    {
        graft::Context ctx(m);
        int cnt = 0;
        for(int i=0; i<pass_cnt; ++i)
        {
            std::for_each(v_keys.begin(), v_keys.end(), [&] (auto& key)
            {
                while( stop < th_count - 1 && cnt == g_count);
                ctx.global.apply(key, f);
                cnt = ++g_count;
            });
        }
        ++stop;
    };
    auto f_b = [&] ()
    {
        graft::Context ctx(m);
        int cnt = 0;
        for(int i=0; i<pass_cnt; ++i)
        {
            std::for_each(v_keys.rbegin(), v_keys.rend(), [&] (auto& key)
            {
                while( stop < th_count - 1 && cnt == g_count);
                ctx.global.apply(key, f);
                cnt = ++g_count;
            });
        }
        ++stop;
    };

    std::thread ths[th_count];
    for(int i=0; i < th_count; ++i)
    {
        ths[i] = (i%2)? std::thread(f_f) : std::thread(f_b);
    }

    int main_count = 0;
    while( stop < th_count )
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        ++g_count;
        ++main_count;
    }

    for(int i=0; i < th_count; ++i)
    {
        ths[i].join();
    }

    uint64_t sum = 0;
    {
        graft::Context ctx(m);
        std::for_each(v_keys.begin(), v_keys.end(), [&] (auto& key)
        {
            uint64_t v = ctx.global[key];
            sum += v;
        });
    }
    EXPECT_EQ(sum, g_count-main_count);
}

/////////////////////////////////
// GraftServerCommonTest fixture

class GraftServerCommonTest : public GraftServerTestBase
{
private:
    class TempCryptoN : public TempCryptoNodeServer
    {
    protected:
        virtual bool onHttpRequest(const http_message *hm, int& status_code, std::string& headers, std::string& data) override
        {
            data = std::string(hm->uri.p, hm->uri.len);
            graft::Context ctx(mainServer.plooper.load()->getGcm());
            int method = ctx.global["method"];
            if(method == METHOD_GET)
            {
                std::string s = ctx.global["requestPath"];
                EXPECT_EQ(s, iocheck);
                iocheck = s += '4'; skip_ctx_check = true;
                ctx.global["requestPath"] = s;
            }
            else
            {
                data = std::string(hm->body.p, hm->body.len);
                graft::Input in; in.load(data.c_str(), data.size());
                Sstr ss = in.get<Sstr>();
                EXPECT_EQ(ss.s, iocheck);
                iocheck = ss.s += '4'; skip_ctx_check = true;
                graft::Output out; out.load(ss);
                auto pair = out.get();
                data = std::string(pair.first, pair.second);
            }
            bool doTimeout = ctx.global.hasKey("doCryptonTimeoutOnce") && (int)ctx.global["doCryptonTimeoutOnce"];
            if(doTimeout)
            {
                ctx.global["doCryptonTimeoutOnce"] = (int)false;
                crypton_ready = false;
                return false;
            }
            headers = "Content-Type: application/json\r\nConnection: close";
            return true;
        }

        virtual void onClose() override
        {
            crypton_ready = true;
        }

    };
public:
    static bool skip_ctx_check;
    static std::string iocheck;
    static std::deque<graft::Status> res_que_action;
    static TempCryptoN tempCryptoN;
    static MainServer mainServer;
    static bool crypton_ready;

    const std::string uri_base = "http://localhost:9084/root/";
    const std::string dapi_url = "http://localhost:9084/dapi";
private:
    static void init_server(graft::Router::Handler3 h3_test)
    {
        assert(h3_test.worker_action);
        graft::Router& http_router = mainServer.router;
        {
            http_router.addRoute("/root/r{id:\\d+}", METHOD_GET, h3_test);
            http_router.addRoute("/root/r{id:\\d+}", METHOD_POST, h3_test);
            http_router.addRoute("/root/aaa/{s1}/bbb/{s2}", METHOD_GET, h3_test);
            graft::registerRTARequests(http_router);
        }

        graft::ConfigOpts copts;
        copts.http_address = "127.0.0.1:9084";
        copts.coap_address = "127.0.0.1:9086";
        copts.http_connection_timeout = .001;
        copts.upstream_request_timeout = .005;
        copts.workers_count = 0;
        copts.worker_queue_len = 0;
        copts.cryptonode_rpc_address = "127.0.0.1:1234";
        copts.timer_poll_interval_ms = 50;

        mainServer.copts = copts;
        mainServer.run();
    }

protected:
    static void SetUpTestCase()
    {
        auto check_ctx = [](auto& ctx, auto& in)
        {
            if(in == "" || skip_ctx_check)
            {
                skip_ctx_check = false;
                return;
            }
            bool bg = ctx.global.hasKey(in);
            bool bl = ctx.local.hasKey(in);
            EXPECT_EQ(true, bg);
            EXPECT_EQ(true, bl);
            if(bg)
            {
                std::string s = ctx.global[in];
                EXPECT_EQ(s, in);
            }
            if(bl)
            {
                std::string s = ctx.local[in];
                EXPECT_EQ(s, in);
            }
        };
        auto get_str = [](const graft::Input& input, graft::Context& ctx, Sstr& ss)
        {
            int method = ctx.global["method"];
            if(method == METHOD_GET)
            {
                std::string s = ctx.global["requestPath"];
                return s;
            }
            else
            {//POST
                ss = input.get<Sstr>();
                std::string s = ss.s;
                return s;
            }
        };
        auto put_str = [](std::string& s, graft::Context& ctx, graft::Output& out, Sstr& ss)
        {
            int method = ctx.global["method"];
            if(method == METHOD_GET)
            {
                ctx.global["requestPath"] = s;
            }
            else
            {//POST
                ss.s = s;
                out.load(ss);
            }
            return s;
        };

        auto pre_action = [&](const graft::Router::vars_t& vars, const graft::Input& input, graft::Context& ctx, graft::Output& output)->graft::Status
        {
            Sstr ss;
            std::string s = get_str(input, ctx, ss);
            EXPECT_EQ(s, iocheck);
            check_ctx(ctx, s);
            iocheck = s += '1';
            put_str(s, ctx, output, ss);
            ctx.global[iocheck] = iocheck;
            ctx.local[iocheck] = iocheck;
            return graft::Status::Ok;
        };
        auto action = [&](const graft::Router::vars_t& vars, const graft::Input& input, graft::Context& ctx, graft::Output& output)->graft::Status
        {
            Sstr ss;
            std::string s = get_str(input, ctx, ss);
            EXPECT_EQ(s, iocheck);
            check_ctx(ctx, s);
            graft::Status res = graft::Status::Ok;
            if(!res_que_action.empty())
            {
                res = res_que_action.front();
                res_que_action.pop_front();
            }
            iocheck = s += '2';
            put_str(s, ctx, output, ss);
            ctx.global[iocheck] = iocheck;
            ctx.local[iocheck] = iocheck;
            return res;
        };
        auto post_action = [&](const graft::Router::vars_t& vars, const graft::Input& input, graft::Context& ctx, graft::Output& output)->graft::Status
        {
            Sstr ss;
            std::string s = get_str(input, ctx, ss);
            EXPECT_EQ(s, iocheck);
            check_ctx(ctx, s);
            iocheck = s += '3';
            put_str(s, ctx, output, ss);
            ctx.global[iocheck] = iocheck;
            ctx.local[iocheck] = iocheck;
            return ctx.local.getLastStatus();
        };

        init_server(graft::Router::Handler3(pre_action, action, post_action));

        tempCryptoN.run();
    }

    static void TearDownTestCase()
    {
        mainServer.stop_and_wait_for();
        tempCryptoN.stop_and_wait_for();
    }
};

bool GraftServerCommonTest::skip_ctx_check = false;
std::string GraftServerCommonTest::iocheck;
std::deque<graft::Status> GraftServerCommonTest::res_que_action;
GraftServerCommonTest::TempCryptoN GraftServerCommonTest::tempCryptoN;
GraftServerCommonTest::MainServer GraftServerCommonTest::mainServer;
bool GraftServerCommonTest::crypton_ready = false;


TEST_F(GraftServerCommonTest, GETtp)
{//GET -> threadPool
    graft::Context ctx(mainServer.plooper.load()->getGcm());
    ctx.global["method"] = METHOD_GET;
    ctx.global["requestPath"] = std::string("0");
    iocheck = "0"; skip_ctx_check = true;
    Client client;
    client.serve(uri_base+"r1");
    EXPECT_EQ(false, client.get_closed());
    std::string res = client.get_body();
    EXPECT_EQ("0123", iocheck);
}

TEST_F(GraftServerCommonTest, clientAcceptTimeout)
{//GET -> timout
    iocheck = ""; skip_ctx_check = true;
    Client client;
    auto begin = std::chrono::high_resolution_clock::now();
    client.serve(uri_base, "Content-Length: 348");
    auto end = std::chrono::high_resolution_clock::now();
    auto int_us = std::chrono::duration_cast<std::chrono::microseconds>(end - begin);
    EXPECT_LT(int_us.count(), 5000); //less than 5 ms
    EXPECT_EQ(true, client.get_closed());
    std::string res = client.get_body();
    EXPECT_EQ("", res);
}

TEST_F(GraftServerTestBase, clientTimeout)
{//it checks that there is no crash
    auto action = [](const graft::Router::vars_t& vars, const graft::Input& input, graft::Context& ctx, graft::Output& output)->graft::Status
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        return graft::Status::Ok;
    };

    MainServer server;
    server.router.addRoute("/timeout", METHOD_POST, {nullptr, action, nullptr});
    server.run();

    Client client;
    client.serve("http://127.0.0.1:9084/timeout", "", "post data", 200);

    server.stop_and_wait_for();
}

//This test requires comparing logging output, their categories with expected.
TEST_F(GraftServerTestBase, logging)
{
    const std::string cat = "handler";

    auto pre = [cat](const graft::Router::vars_t& vars, const graft::Input& input, graft::Context& ctx, graft::Output& output)->graft::Status
    {
        EXPECT_EQ(mlog_current_log_category, cat);
        LOG_PRINT_L0("This is pre");
        return graft::Status::Ok;
    };

    auto worker = [cat](const graft::Router::vars_t& vars, const graft::Input& input, graft::Context& ctx, graft::Output& output)->graft::Status
    {
        EXPECT_EQ(mlog_current_log_category, cat);
        LOG_PRINT_L0("This is worker");
        return graft::Status::Ok;
    };

    auto post = [cat](const graft::Router::vars_t& vars, const graft::Input& input, graft::Context& ctx, graft::Output& output)->graft::Status
    {
        EXPECT_EQ(mlog_current_log_category, cat);
        LOG_PRINT_L0("This is post");
        return graft::Status::Ok;
    };

    MainServer server;
    server.router.addRoute("/logging", METHOD_GET, {pre, worker, post, cat.c_str()});
    server.run();

    Client client;
    client.serve("http://127.0.0.1:9084/logging");

    server.stop_and_wait_for();
}

TEST_F(GraftServerCommonTest, cryptonTimeout)
{//GET -> threadPool -> CryptoNode -> timeout
    graft::Context ctx(mainServer.plooper.load()->getGcm());
    ctx.global["method"] = METHOD_GET;
    ctx.global["requestPath"] = std::string("0");
    iocheck = "0"; skip_ctx_check = true;
    res_que_action.clear();
    res_que_action.push_back(graft::Status::Forward);
    res_que_action.push_back(graft::Status::Ok);
    Client client;
    ctx.global["doCryptonTimeoutOnce"] = (int)true;
    auto begin = std::chrono::high_resolution_clock::now();
    client.serve(uri_base+"r2");
    auto end = std::chrono::high_resolution_clock::now();
    EXPECT_EQ(false, client.get_closed());
    EXPECT_EQ(client.get_resp_code(), 500);
    EXPECT_EQ(client.get_body(), "");
    auto int_us = std::chrono::duration_cast<std::chrono::microseconds>(end - begin);
    EXPECT_LT(int_us.count(), 10000); //less than 10 ms
    while(!crypton_ready)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

TEST_F(GraftServerCommonTest, timerEvents)
{
    constexpr int ms_all = 5000, ms_step = 100, N = 5;
    int cntrs_all[N]; for(int& v:cntrs_all){ v = 0; }
    int cntrs[N]; for(int& v:cntrs){ v = 0; }
    auto finish = std::chrono::steady_clock::now()+std::chrono::milliseconds(ms_all);
    auto make_timer = [=,&cntrs_all,&cntrs](int i, int ms)
    {
        auto action = [=,&cntrs_all,&cntrs](const graft::Router::vars_t& vars, const graft::Input& input, graft::Context& ctx, graft::Output& output)->graft::Status
        {
            ++cntrs_all[i];
            if(ctx.local.getLastStatus() == graft::Status::Forward)
                return graft::Status::Ok;
            EXPECT_TRUE(cntrs[i]==0 && ctx.local.getLastStatus() == graft::Status::None
                        || ctx.local.getLastStatus() == graft::Status::Ok);
            if(finish < std::chrono::steady_clock::now()) return graft::Status::Stop;
            ++cntrs[i];
            Sstr ss; ss.s = std::to_string(cntrs[i]) + " " + std::to_string(cntrs[i]);
            output.load(ss);
            return graft::Status::Forward;
        };

        mainServer.plooper.load()->addPeriodicTask(
                    graft::Router::Handler3(nullptr, action, nullptr),
                    std::chrono::milliseconds(ms)
                    );
    };

    for(int i=0; i<N; ++i)
    {
        make_timer(i, (i+1)*ms_step);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(int(ms_all*1.1)));

    for(int i=0; i<N; ++i)
    {
        int n = ms_all/((i+1)*ms_step);
        n -= (mainServer.plooper.load()->getCopts().upstream_request_timeout*1000*n)/((i+1)*ms_step);
        EXPECT_LE(n-2, cntrs[i]);
        EXPECT_LE(cntrs[i], n+1);
        EXPECT_EQ(cntrs_all[i]-1, 2*cntrs[i]);
    }
}

TEST_F(GraftServerCommonTest, GETtpCNtp)
{//GET -> threadPool -> CryptoNode -> threadPool
    graft::Context ctx(mainServer.plooper.load()->getGcm());
    ctx.global["method"] = METHOD_GET;
    ctx.global["requestPath"] = std::string("0");
    iocheck = "0"; skip_ctx_check = true;
    res_que_action.clear();
    res_que_action.push_back(graft::Status::Forward);
    res_que_action.push_back(graft::Status::Ok);
    Client client;
    client.serve(uri_base+"r2");
    EXPECT_EQ(false, client.get_closed());
    std::string res = client.get_body();
    EXPECT_EQ("01234123", iocheck);
}

TEST_F(GraftServerCommonTest, POSTtp)
{//POST -> threadPool
    graft::Context ctx(mainServer.plooper.load()->getGcm());
    ctx.global["method"] = METHOD_POST;
    std::string jsonx = "{\"s\":\"0\"}";
    iocheck = "0"; skip_ctx_check = true;
    Client client;
    std::string res = client.serve_json_res(uri_base+"r3", jsonx);
    EXPECT_EQ("{\"s\":\"0123\"}", res);
    graft::Input response;
    response.load(res.data(), res.length());
    Sstr test_response = response.get<Sstr>();
    EXPECT_EQ("0123", test_response.s);
    EXPECT_EQ("0123", iocheck);
}

TEST_F(GraftServerCommonTest, POSTtpCNtp)
{//POST -> threadPool -> CryptoNode -> threadPool
    graft::Context ctx(mainServer.plooper.load()->getGcm());
    ctx.global["method"] = METHOD_POST;
    std::string jsonx = "{\"s\":\"0\"}";
    iocheck = "0"; skip_ctx_check = true;
    res_que_action.clear();
    res_que_action.push_back(graft::Status::Forward);
    res_que_action.push_back(graft::Status::Ok);
    Client client;
    std::string res = client.serve_json_res(uri_base+"r4", jsonx);
    EXPECT_EQ("{\"s\":\"01234123\"}", res);
    graft::Input response;
    response.load(res.data(), res.length());
    Sstr test_response = response.get<Sstr>();
    EXPECT_EQ("01234123", test_response.s);
    EXPECT_EQ("01234123", iocheck);
}

TEST_F(GraftServerCommonTest, clPOSTtp)
{//POST cmdline -> threadPool
    graft::Context ctx(mainServer.plooper.load()->getGcm());
    ctx.global["method"] = METHOD_POST;
    std::string jsonx = "{\\\"s\\\":\\\"0\\\"}";
    iocheck = "0"; skip_ctx_check = true;
    {
        std::string res = send_request(uri_base+"r3", jsonx);
        EXPECT_EQ("{\"s\":\"0123\"}", res);
        graft::Input response;
        response.load(res.data(), res.length());
        Sstr test_response = response.get<Sstr>();
        EXPECT_EQ("0123", test_response.s);
        EXPECT_EQ("0123", iocheck);
    }
}

TEST_F(GraftServerCommonTest, clPOSTtpCNtp)
{//POST cmdline -> threadPool -> CryptoNode -> threadPool
    graft::Context ctx(mainServer.plooper.load()->getGcm());
    ctx.global["method"] = METHOD_POST;
    std::string jsonx = "{\\\"s\\\":\\\"0\\\"}";
    iocheck = "0"; skip_ctx_check = true;
    res_que_action.clear();
    res_que_action.push_back(graft::Status::Forward);
    res_que_action.push_back(graft::Status::Ok);
    {
        std::string res = send_request(uri_base+"r4", jsonx);
        EXPECT_EQ("{\"s\":\"01234123\"}", res);
        graft::Input response;
        response.load(res.data(), res.length());
        Sstr test_response = response.get<Sstr>();
        EXPECT_EQ("01234123", test_response.s);
        EXPECT_EQ("01234123", iocheck);
    }
}

/////////////////////////////////
// GraftServerPostponeTest fixture

class GraftServerPostponeTest : public GraftServerTestBase
{
public:
    class TempCryptoN : public TempCryptoNodeServer
    {
    public:
        bool do_callback = true;
    protected:
        virtual bool onHttpRequest(const http_message *hm, int& status_code, std::string& headers, std::string& data) override
        {
            data = std::string(hm->body.p, hm->body.len);
            std::string method(hm->method.p, hm->method.len);
            graft::Input in; in.load(data);
            Sstr ss = in.get<Sstr>();
            headers = "Content-Type: application/json\r\nConnection: close";

            if(!do_callback) return true;

            std::string uuid_str = ss.s;
            auto send_callback = [uuid_str]()
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                Client callback_client;
                std::string url = "http://127.0.0.1:9084/callback/" + uuid_str;
                std::string post_data = "it is callback post data";
                callback_client.serve(url,"",post_data);

                EXPECT_EQ(false, callback_client.get_closed());
                EXPECT_EQ(200, callback_client.get_resp_code());
                std::string s = callback_client.get_body();
                EXPECT_EQ(s, post_data);
            };
            std::thread th(send_callback);
            th.detach();

            return true;
        }
    };
};

TEST_F(GraftServerPostponeTest, common)
{
    auto callback_action = [](const graft::Router::vars_t& vars, const graft::Input& input, graft::Context& ctx, graft::Output& output)->graft::Status
    {
        output.body = input.data();
        assert(vars.count("id") == 1);
        std::string id = vars.find("id")->second;
        boost::uuids::string_generator sg;
        boost::uuids::uuid uuid = sg(id);

        ctx.setNextTaskId(uuid);
        return graft::Status::Ok;
    };

    std::string postpone_result = "this is postpone result";

    auto action = [&](const graft::Router::vars_t& vars, const graft::Input& input, graft::Context& ctx, graft::Output& output)->graft::Status
    {
        switch(ctx.local.getLastStatus())
        {
        case graft::Status::None:
        {
            boost::uuids::uuid uuid = ctx.getId();
            Sstr ss; ss.s = boost::uuids::to_string(uuid);
            output.load(ss);
            return graft::Status::Forward;
        } break;
        case graft::Status::Forward:
        {
            boost::uuids::uuid uuid = ctx.getId();
            return graft::Status::Postpone;
        } break;
        case graft::Status::Postpone:
        {
            boost::uuids::uuid uuid = ctx.getId();
            output.body = postpone_result;
            return graft::Status::Ok;
        } break;
        }
    };

    TempCryptoN crypton;
    crypton.run();
    MainServer mainServer;
    mainServer.router.addRoute("/json_rpc",METHOD_POST,{nullptr,action,nullptr});
    mainServer.router.addRoute("/callback/{id:[0-9a-fA-F-]+}",METHOD_POST,{nullptr,callback_action,nullptr});
    mainServer.run();

    std::string post_data = "some data";
    Client client;
    client.serve("http://localhost:9084/json_rpc", "", post_data);

    EXPECT_EQ(false, client.get_closed());
    EXPECT_EQ(200, client.get_resp_code());
    std::string s = client.get_body();
    EXPECT_EQ(s, postpone_result);

    //make hang postpones
    crypton.do_callback = false;
    client.serve("http://localhost:9084/json_rpc", "", post_data);
    EXPECT_EQ(false, client.get_closed());
    EXPECT_EQ(500, client.get_resp_code());
    std::string body = client.get_body();
    EXPECT_EQ(body, "Postpone task response timeout");

    mainServer.stop_and_wait_for();
    crypton.stop_and_wait_for();
}

/////////////////////////////////
// GraftServerForwardTest fixture

class GraftServerForwardTest : public GraftServerTestBase
{
public:
    class TempCryptoN : public TempCryptoNodeServer
    {
    protected:
        virtual bool onHttpRequest(const http_message *hm, int& status_code, std::string& headers, std::string& data) override
        {
            data = std::string(hm->body.p, hm->body.len);
            std::string method(hm->method.p, hm->method.len);
            headers = "Content-Type: application/json\r\nConnection: close";
            return true;
        }
    };
};

TEST_F(GraftServerForwardTest, inner)
{
    TempCryptoN crypton;
    crypton.run();
    MainServer mainServer;
    graft::registerForwardRequests(mainServer.router);
    mainServer.run();

    std::string post_data = "some data";
    Client client;
    client.serve("http://localhost:9084/json_rpc", "", post_data);
    EXPECT_EQ(false, client.get_closed());
    EXPECT_EQ(200, client.get_resp_code());
    std::string s = client.get_body();
    EXPECT_EQ(s, post_data);

    mainServer.stop_and_wait_for();
    crypton.stop_and_wait_for();
}

GRAFT_DEFINE_IO_STRUCT(GetVersionResp,
                       (std::string, status),
                       (uint32_t, version)
                       );

GRAFT_DEFINE_JSON_RPC_RESPONSE_RESULT(JRResponseResult, GetVersionResp);

//you can run cryptonodes and enable this tests using
//--gtest_also_run_disabled_tests
//https://github.com/google/googletest/blob/master/googletest/docs/advanced.md
TEST_F(GraftServerForwardTest, DISABLED_getVersion)
{
    MainServer mainServer;
    mainServer.copts.cryptonode_rpc_address = "localhost:38281";
    graft::registerForwardRequests(mainServer.router);
    mainServer.run();

    graft::JsonRpcRequestHeader request;
    request.method = "get_version";

    std::string post_data = request.toJson().GetString();
    Client client;
    client.serve("http://localhost:9084/json_rpc", "", post_data);
    EXPECT_EQ(false, client.get_closed());
    EXPECT_EQ(200, client.get_resp_code());
    std::string response_s = client.get_body();

    graft::Input in; in.load(response_s);
    EXPECT_NO_THROW( JRResponseResult result = in.get<JRResponseResult>() );

    mainServer.stop_and_wait_for();
}

/////////////////////////////////
// GraftServerBlockingTest fixture

class GraftServerBlockingTest : public GraftServerTestBase
{
public:
    class TempCryptoN : public TempCryptoNodeServer
    {
    public:
        bool ignore = false;
        std::string answer;
        std::string body;
    protected:
        virtual bool onHttpRequest(const http_message *hm, int& status_code, std::string& headers, std::string& data) override
        {
            body = std::string(hm->body.p, hm->body.len);
            std::string method(hm->method.p, hm->method.len);
            if(ignore) return false;
            data = answer;
            headers = "Content-Type: application/json\r\nConnection: close";
            return true;
        }
    };
};

TEST_F(GraftServerBlockingTest, common)
{
    TempCryptoN crypton;
    crypton.run();
    auto pre_action = [&](const graft::Router::vars_t& vars, const graft::Input& input, graft::Context& ctx, graft::Output& output)->graft::Status
    {
        Sstr ss; ss.s = "my string";
        graft::Output out; out.load(ss);
        //check io thread exceprtion
        crypton.answer = "crypton answer";
        graft::Input res;
        std::string err;
        int state = 0;
        try
        {
            graft::TaskManager::sendUpstreamBlocking(out, res, err);
            state = 1;
        }
        catch(std::exception& ex)
        {
            state = 2;
        }
        EXPECT_EQ(state,2);
        return graft::Status::Ok;
    };
    auto action = [&](const graft::Router::vars_t& vars, const graft::Input& input, graft::Context& ctx, graft::Output& output)->graft::Status
    {
        Sstr ss; ss.s = "my string";
        graft::Output out; out.load(ss);
        //without error
        crypton.answer = "crypton answer";
        graft::Input res;
        std::string err;
        graft::TaskManager::sendUpstreamBlocking(out, res, err);
        EXPECT_EQ(res.body, crypton.answer);
        EXPECT_EQ(err.empty(), true);
        //with error
        res.body.clear();
        crypton.ignore = true;
        graft::TaskManager::sendUpstreamBlocking(out, res, err);
        EXPECT_EQ(err.empty(), false);
        return graft::Status::Ok;
    };

    MainServer mainServer;
    mainServer.router.addRoute("/json_block", METHOD_POST|METHOD_GET,
                               graft::Router::Handler3(pre_action, action, nullptr));
    mainServer.run();

    std::string post_data = "some data";
    Client client;
    client.serve("http://localhost:9084/json_block", "", post_data);
    EXPECT_EQ(false, client.get_closed());
    EXPECT_EQ(200, client.get_resp_code());

    mainServer.stop_and_wait_for();
    crypton.stop_and_wait_for();
}
