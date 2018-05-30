#include <gtest/gtest.h>
#include "context.h"
#include "graft_manager.h"
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
#include <deque>

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
    s.erase(remove_if(s.begin(), s.end(), isspace), s.end());
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
    {//get pass count so that overall will take about 100 ms
        graft::Context ctx(m);
        auto begin = std::chrono::high_resolution_clock::now();
        std::for_each(v_keys.begin(), v_keys.end(), [&] (auto& key)
        {
            ctx.global.apply(key, f);
            ++g_count;
        });
        auto end = std::chrono::high_resolution_clock::now();
        std::chrono::high_resolution_clock::duration d = std::chrono::milliseconds(100);
        pass_cnt = d.count() / (end - begin).count();
    }

    //forward and backward passes
    bool stop = false;

    auto f_f = [&] ()
    {
        graft::Context ctx(m);
        int cnt = 0;
        for(int i=0; i<pass_cnt; ++i)
        {
            std::for_each(v_keys.begin(), v_keys.end(), [&] (auto& key)
            {
                while(!stop && cnt == g_count);
                ctx.global.apply(key, f);
                cnt = ++g_count;
            });
        }
        stop = true;
    };
    auto f_b = [&] ()
    {
        graft::Context ctx(m);
        int cnt = 0;
        for(int i=0; i<pass_cnt; ++i)
        {
            std::for_each(v_keys.rbegin(), v_keys.rend(), [&] (auto& key)
            {
                while(!stop && cnt == g_count);
                ctx.global.apply(key, f);
                cnt = ++g_count;
            });
        }
        stop = true;
    };

    std::thread tf(f_f);
    std::thread tb(f_b);

    int main_count = 0;
    while( !stop )
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        ++g_count;
        ++main_count;
    }
    tf.join();
    tb.join();

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
// GraftServerTest fixture

class GraftServerTest : public ::testing::Test
{
public:
    static std::string iocheck;
    static bool skip_ctx_check;
    static std::deque<graft::Status> res_que_action;
    static graft::Router::Handler3 h3_test;
    static std::thread t_CN;
    static std::thread t_srv;
    static bool run_server_ready;
    static graft::Manager* pmanager;

    const std::string uri_base = "http://localhost:9084/root/";
    const std::string dapi_url = "http://localhost:9084/dapi";

private:
    //Server to simulate CryptoNode (its object is created in non-main thread)
    class TempCryptoNodeServer
    {
    public:
        static void run()
        {
            mg_mgr mgr;
            mg_mgr_init(&mgr, NULL, 0);
            mg_connection *nc = mg_bind(&mgr, "1234", ev_handler_http);
            mg_set_protocol_http_websocket(nc);
            ready = true;
            for (;;) {
                mg_mgr_poll(&mgr, 1000);
                if(stop) break;
            }
            mg_mgr_free(&mgr);
        }
    private:
        static void ev_handler_empty(mg_connection *client, int ev, void *ev_data)
        {
        }
        static void ev_handler_http(mg_connection *client, int ev, void *ev_data)
        {
            switch (ev)
            {
            case MG_EV_HTTP_REQUEST:
            {
                mg_set_timer(client, 0);

                struct http_message *hm = (struct http_message *) ev_data;
                std::string data(hm->uri.p, hm->uri.len);
                graft::Context ctx(pmanager->get_gcm());
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
                mg_send_head(client, 200, data.size(), "Content-Type: application/json\r\nConnection: close");
                mg_send(client, data.c_str(), data.size());
                client->flags |= MG_F_SEND_AND_CLOSE;
            } break;
            case MG_EV_ACCEPT:
            {
                mg_set_timer(client, mg_time() + 1000);
            } break;
            case MG_EV_TIMER:
            {
                mg_set_timer(client, 0);
                client->handler = ev_handler_empty; //without this we will get MG_EV_HTTP_REQUEST
                client->flags |= MG_F_CLOSE_IMMEDIATELY;
             } break;
            default:
                break;
            }
        }
    public:
        static bool ready;
        static bool stop;
    };

    //prepare and run GraftServer (it is called in non-main thread)
    static void run_server()
    {
        assert(h3_test.worker_action);
        graft::Router http_router;
        {
            http_router.addRoute("/root/r{id:\\d+}", METHOD_GET, h3_test);
            http_router.addRoute("/root/r{id:\\d+}", METHOD_POST, h3_test);
            http_router.addRoute("/root/aaa/{s1}/bbb/{s2}", METHOD_GET, h3_test);
            graft::registerRTARequests(http_router);
        }

        graft::ServerOpts sopts;
        sopts.http_address = "127.0.0.1:9084";
        sopts.coap_address = "127.0.0.1:9086";
        sopts.http_connection_timeout = .001;
        sopts.workers_count = 0;
        sopts.worker_queue_len = 0;
        sopts.cryptonode_rpc_address = "127.0.0.1:1234";

        graft::Manager manager(sopts);
        pmanager = &manager;

        manager.addRouter(http_router);
        bool res = manager.enableRouting();
        EXPECT_EQ(res, true);

        graft::GraftServer gs;
        gs.serve(manager.get_mg_mgr());
    }

public:
    //http client (its objects are created in the main thread)
    class Client : public graft::StaticMongooseHandler<Client>
    {
    public:
        Client()
        {
            mg_mgr_init(&m_mgr, nullptr, nullptr);
        }

        void serve(const std::string& url, const std::string& extra_headers = std::string(), const std::string& post_data = std::string())
        {
            m_exit = false; m_closed = false;
            client = mg_connect_http(&m_mgr, static_ev_handler, url.c_str(),
                                     (extra_headers.empty())? nullptr : extra_headers.c_str(),
                                     (post_data.empty())? nullptr : post_data.c_str()); //last nullptr means GET
            assert(client);
            client->user_data = this;
            while(!m_exit)
            {
                mg_mgr_poll(&m_mgr, 1000);
            }
        }

        std::string serve_json_res(const std::string& url, const std::string& json_data)
        {
            serve(url, "Content-Type: application/json\r\n", json_data);
            EXPECT_EQ(false, get_closed());
            return get_body();
        }

        bool get_closed(){ return m_closed; }
        std::string get_body(){ return m_body; }
        std::string get_message(){ return m_message; }

        ~Client()
        {
            mg_mgr_free(&m_mgr);
        }
    private:
        friend class graft::StaticMongooseHandler<Client>;
        void ev_handler(mg_connection* client, int ev, void *ev_data)
        {
            assert(client == this->client);
            switch(ev)
            {
            case MG_EV_CONNECT:
            {
                int& err = *static_cast<int*>(ev_data);
                if(err != 0)
                {
                    std::ostringstream s;
                    s << "connect() failed: " << strerror(err);
                    m_message = s.str();
                    m_exit = true;
                }
            } break;
            case MG_EV_HTTP_REPLY:
            {
                http_message* hm = static_cast<http_message*>(ev_data);
                m_body = std::string(hm->body.p, hm->body.len);
                client->flags |= MG_F_CLOSE_IMMEDIATELY;
                client->handler = static_empty_ev_handler;
                m_exit = true;
            } break;
            case MG_EV_RECV:
            {
                int cnt = *static_cast<int*>(ev_data);
                mbuf& buf = client->recv_mbuf;
                m_message = std::string(buf.buf, buf.len);
            } break;
            case MG_EV_CLOSE:
            {
                client->handler = static_empty_ev_handler;
                m_closed = true;
                m_exit = true;
            } break;
            }
        }
    private:
        bool m_exit = false;
        bool m_closed = false;
        mg_mgr m_mgr;
        mg_connection* client = nullptr;
        std::string m_body;
        std::string m_message;
    };

public:
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
        return res;
    }

    static std::string send_request(const std::string &url, const std::string &json_data)
    {
        std::ostringstream s;
        s << "curl --data \"" << json_data << "\" " << url;
        std::string ss = s.str();
        return run_cmdline_read(ss.c_str());
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

        h3_test = graft::Router::Handler3(pre_action, action, post_action);

        t_CN = std::thread([]{ TempCryptoNodeServer::run(); });
        t_srv = std::thread([]{ run_server(); });

        while(!TempCryptoNodeServer::ready || !graft::GraftServer::ready)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    static void TearDownTestCase()
    {
        {
            Client client;
            client.serve("http://localhost:9084/root/exit");
        }
        TempCryptoNodeServer::stop = true;

        t_srv.join();
        t_CN.join();
    }

    virtual void SetUp() override
    { }
    virtual void TearDown() override
    { }

};

std::string GraftServerTest::iocheck;
bool GraftServerTest::skip_ctx_check = false;
std::deque<graft::Status> GraftServerTest::res_que_action;
graft::Router::Handler3 GraftServerTest::h3_test;
std::thread GraftServerTest::t_CN;
std::thread GraftServerTest::t_srv;
bool GraftServerTest::run_server_ready = false;
graft::Manager* GraftServerTest::pmanager = nullptr;

bool GraftServerTest::TempCryptoNodeServer::ready = false;
bool GraftServerTest::TempCryptoNodeServer::stop = false;

TEST_F(GraftServerTest, GETtp)
{//GET -> threadPool
    graft::Context ctx(pmanager->get_gcm());
    ctx.global["method"] = METHOD_GET;
    ctx.global["requestPath"] = std::string("0");
    iocheck = "0"; skip_ctx_check = true;
    Client client;
    client.serve(uri_base+"r1");
    EXPECT_EQ(false, client.get_closed());
    std::string res = client.get_body();
    EXPECT_EQ("0123", iocheck);
}

TEST_F(GraftServerTest, timeout)
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

TEST_F(GraftServerTest, GETtpCNtp)
{//GET -> threadPool -> CryptoNode -> threadPool
    graft::Context ctx(pmanager->get_gcm());
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

TEST_F(GraftServerTest, POSTtp)
{//POST -> threadPool
    graft::Context ctx(pmanager->get_gcm());
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

TEST_F(GraftServerTest, POSTtpCNtp)
{//POST -> threadPool -> CryptoNode -> threadPool
    graft::Context ctx(pmanager->get_gcm());
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

TEST_F(GraftServerTest, clPOSTtp)
{//POST cmdline -> threadPool
    graft::Context ctx(pmanager->get_gcm());
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

TEST_F(GraftServerTest, clPOSTtpCNtp)
{//POST cmdline -> threadPool -> CryptoNode -> threadPool
    graft::Context ctx(pmanager->get_gcm());
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

TEST_F(GraftServerTest, testSaleRequest)
{
    std::string sale_url(dapi_url + "/sale");
    graft::Input response;
    std::string res;
    ErrorResponse error_response;
    Client client;

    std::string wallet_address("F4TD8JVFx2xWLeL3qwSmxLWVcPbmfUM1PanF2VPnQ7Ep2LjQCVncxqH3EZ3XCCuqQci5xi5GCYR7KRoytradoJg71DdfXpz");

    std::string empty_data_request("{\"Address\":\"\",\"SaleDetails\":\"\",\"Amount\":\"10.0\"}");
    res = client.serve_json_res(sale_url, empty_data_request);
    response.load(res.data(), res.length());
    error_response = response.get<ErrorResponse>();
    EXPECT_EQ(ERROR_INVALID_PARAMS, error_response.code);
    EXPECT_EQ(MESSAGE_INVALID_PARAMS, error_response.message);

    std::string error_balance_request("{\"Address\":\"" + wallet_address
                                      + "\",\"SaleDetails\":\"\",\"Amount\":\"fffffffff\"}");
    res = client.serve_json_res(sale_url, error_balance_request);
    response.load(res.data(), res.length());
    error_response = response.get<ErrorResponse>();
    EXPECT_EQ(ERROR_AMOUNT_INVALID, error_response.code);
    EXPECT_EQ(MESSAGE_AMOUNT_INVALID, error_response.message);

    std::string custom_pid("test");

    std::string custom_pid_request("{\"Address\":\"" + wallet_address
                                   + "\",\"SaleDetails\":\"\",\"PaymentID\":\""
                                   + custom_pid + "\",\"Amount\":\"10.0\"}");
    res = client.serve_json_res(sale_url, custom_pid_request);
    response.load(res.data(), res.length());
    graft::SaleResponse sale_response = response.get<graft::SaleResponse>();
    EXPECT_EQ(custom_pid, sale_response.PaymentID);
}

TEST_F(GraftServerTest, testSaleStatusRequest)
{
    std::string sale_status_url(dapi_url + "/sale_status");
    graft::Input response;
    std::string res;
    ErrorResponse error_response;
    Client client;

    std::string empty_data_request("{\"PaymentID\":\"\"}");
    res = client.serve_json_res(sale_status_url, empty_data_request);
    response.load(res.data(), res.length());
    error_response = response.get<ErrorResponse>();
    EXPECT_EQ(ERROR_PAYMENT_ID_INVALID, error_response.code);
    EXPECT_EQ(MESSAGE_PAYMENT_ID_INVALID, error_response.message);

    std::string wrong_data_request("{\"PaymentID\":\"zzzzzzzzzzzzzzzzzzz\"}");
    res = client.serve_json_res(sale_status_url, wrong_data_request);
    response.load(res.data(), res.length());
    error_response = response.get<ErrorResponse>();
    EXPECT_EQ(ERROR_PAYMENT_ID_INVALID, error_response.code);
    EXPECT_EQ(MESSAGE_PAYMENT_ID_INVALID, error_response.message);
}

TEST_F(GraftServerTest, testRejectSaleRequest)
{
    std::string reject_sale_url(dapi_url + "/reject_sale");
    graft::Input response;
    std::string res;
    ErrorResponse error_response;
    Client client;

    std::string empty_data_request("{\"PaymentID\":\"\"}");
    res = client.serve_json_res(reject_sale_url, empty_data_request);
    response.load(res.data(), res.length());
    error_response = response.get<ErrorResponse>();
    EXPECT_EQ(ERROR_PAYMENT_ID_INVALID, error_response.code);
    EXPECT_EQ(MESSAGE_PAYMENT_ID_INVALID, error_response.message);

    std::string wrong_data_request("{\"PaymentID\":\"zzzzzzzzzzzzzzzzzzz\"}");
    res = client.serve_json_res(reject_sale_url, wrong_data_request);
    response.load(res.data(), res.length());
    error_response = response.get<ErrorResponse>();
    EXPECT_EQ(ERROR_PAYMENT_ID_INVALID, error_response.code);
    EXPECT_EQ(MESSAGE_PAYMENT_ID_INVALID, error_response.message);
}

TEST_F(GraftServerTest, testSaleDetailsRequest)
{
    std::string sale_details_url(dapi_url + "/sale_details");
    graft::Input response;
    std::string res;
    ErrorResponse error_response;
    Client client;

    std::string empty_data_request("{\"PaymentID\":\"\"}");
    res = client.serve_json_res(sale_details_url, empty_data_request);
    response.load(res.data(), res.length());
    error_response = response.get<ErrorResponse>();
    EXPECT_EQ(ERROR_PAYMENT_ID_INVALID, error_response.code);
    EXPECT_EQ(MESSAGE_PAYMENT_ID_INVALID, error_response.message);

    std::string wrong_data_request("{\"PaymentID\":\"zzzzzzzzzzzzzzzzzzz\"}");
    res = client.serve_json_res(sale_details_url, wrong_data_request);
    response.load(res.data(), res.length());
    error_response = response.get<ErrorResponse>();
    EXPECT_EQ(ERROR_PAYMENT_ID_INVALID, error_response.code);
    EXPECT_EQ(MESSAGE_PAYMENT_ID_INVALID, error_response.message);
}

TEST_F(GraftServerTest, testPayRequest)
{
    std::string pay_url(dapi_url + "/pay");
    graft::Input response;
    std::string res;
    ErrorResponse error_response;
    Client client;

    std::string amount("10.0");

    std::string empty_wallet_address("");
    std::string empty_payment_id("");
    std::string empty_data_request("{\"Address\":\"" + empty_wallet_address
                                   + "\",\"BlockNumber\":0,\"PaymentID\":\"" + empty_payment_id
                                   + "\",\"Amount\":\"" + amount + "\"}");
    res = client.serve_json_res(pay_url, empty_data_request);
    response.load(res.data(), res.length());
    error_response = response.get<ErrorResponse>();
    EXPECT_EQ(ERROR_INVALID_PARAMS, error_response.code);
    EXPECT_EQ(MESSAGE_INVALID_PARAMS, error_response.message);

    std::string wallet_address("F4TD8JVFx2xWLeL3qwSmxLWVcPbmfUM1PanF2VPnQ7Ep2LjQCVncxqH3EZ3XCCuqQci5xi5GCYR7KRoytradoJg71DdfXpz");
    std::string payment_id("22222222222222222222");

    std::string empty_amount("");
    std::string empty_balance_request("{\"Address\":\"" + wallet_address
                                      + "\",\"BlockNumber\":0,\"PaymentID\":\"" + payment_id
                                      + "\",\"Amount\":\"" + empty_amount + "\"}");
    res = client.serve_json_res(pay_url, empty_balance_request);
    response.load(res.data(), res.length());
    error_response = response.get<ErrorResponse>();
    EXPECT_EQ(ERROR_INVALID_PARAMS, error_response.code);
    EXPECT_EQ(MESSAGE_INVALID_PARAMS, error_response.message);

    std::string sale_url(dapi_url + "/sale");
    graft::SaleResponse sale_response;
    std::string correct_sale_request("{\"Address\":\"" + wallet_address
                                     + "\",\"SaleDetails\":\"dddd\",\"Amount\":\"10.0\"}");
    res = client.serve_json_res(sale_url, correct_sale_request);
    response.load(res.data(), res.length());
    sale_response = response.get<graft::SaleResponse>();
    EXPECT_EQ(36, sale_response.PaymentID.length());
    ASSERT_FALSE(sale_response.BlockNumber < 0); //TODO: Change to `BlockNumber <= 0`

    std::string error_amount("ggggg");
    std::string error_balance_request("{\"Address\":\"" + wallet_address
                                      + "\",\"BlockNumber\":0,\"PaymentID\":\"" + sale_response.PaymentID
                                      + "\",\"Amount\":\"" + error_amount + "\"}");
    res = client.serve_json_res(pay_url, error_balance_request);
    response.load(res.data(), res.length());
    error_response = response.get<ErrorResponse>();
    EXPECT_EQ(ERROR_AMOUNT_INVALID, error_response.code);
    EXPECT_EQ(MESSAGE_AMOUNT_INVALID, error_response.message);
}

TEST_F(GraftServerTest, testPayStatusRequest)
{
    std::string pay_status_url(dapi_url + "/pay_status");
    graft::Input response;
    std::string res;
    ErrorResponse error_response;
    Client client;

    std::string empty_data_request("{\"PaymentID\":\"\"}");
    res = client.serve_json_res(pay_status_url, empty_data_request);
    response.load(res.data(), res.length());
    error_response = response.get<ErrorResponse>();
    EXPECT_EQ(ERROR_PAYMENT_ID_INVALID, error_response.code);
    EXPECT_EQ(MESSAGE_PAYMENT_ID_INVALID, error_response.message);

    std::string wrong_data_request("{\"PaymentID\":\"zzzzzzzzzzzzzzzzzzz\"}");
    res = client.serve_json_res(pay_status_url, wrong_data_request);
    response.load(res.data(), res.length());
    error_response = response.get<ErrorResponse>();
    EXPECT_EQ(ERROR_PAYMENT_ID_INVALID, error_response.code);
    EXPECT_EQ(MESSAGE_PAYMENT_ID_INVALID, error_response.message);
}

TEST_F(GraftServerTest, testRejectPayRequest)
{
    std::string reject_pay_url(dapi_url + "/reject_pay");
    graft::Input response;
    std::string res;
    ErrorResponse error_response;
    Client client;

    std::string empty_data_request("{\"PaymentID\":\"\"}");
    res = client.serve_json_res(reject_pay_url, empty_data_request);
    response.load(res.data(), res.length());
    error_response = response.get<ErrorResponse>();
    EXPECT_EQ(ERROR_PAYMENT_ID_INVALID, error_response.code);
    EXPECT_EQ(MESSAGE_PAYMENT_ID_INVALID, error_response.message);

    std::string wrong_data_request("{\"PaymentID\":\"zzzzzzzzzzzzzzzzzzz\"}");
    res = client.serve_json_res(reject_pay_url, wrong_data_request);
    response.load(res.data(), res.length());
    error_response = response.get<ErrorResponse>();
    EXPECT_EQ(ERROR_PAYMENT_ID_INVALID, error_response.code);
    EXPECT_EQ(MESSAGE_PAYMENT_ID_INVALID, error_response.message);
}

TEST_F(GraftServerTest, testRTARejectSaleFlow)
{
    graft::Input response;
    std::string res;
    Client client;

    std::string sale_url(dapi_url + "/sale");
    graft::SaleResponse sale_response;
    std::string correct_sale_request("{\"Address\":\"F4TD8JVFx2xWLeL3qwSmxLWVcPbmfUM1PanF2VPnQ7Ep2LjQCVncxqH3EZ3XCCuqQci5xi5GCYR7KRoytradoJg71DdfXpz\",\"SaleDetails\":\"dddd\",\"Amount\":\"10.0\"}");
    res = client.serve_json_res(sale_url, correct_sale_request);
    response.load(res.data(), res.length());
    sale_response = response.get<graft::SaleResponse>();
    EXPECT_EQ(36, sale_response.PaymentID.length());
    ASSERT_FALSE(sale_response.BlockNumber < 0); //TODO: Change to `BlockNumber <= 0`

    std::string sale_status_url(dapi_url + "/sale_status");
    graft::SaleStatusResponse sale_status_response;
    std::string sale_status_request("{\"PaymentID\":\"" + sale_response.PaymentID + "\"}");
    res = client.serve_json_res(sale_status_url, sale_status_request);
    response.load(res.data(), res.length());
    sale_status_response = response.get<graft::SaleStatusResponse>();
    EXPECT_EQ(static_cast<int>(graft::RTAStatus::Waiting), sale_status_response.Status);

    std::string reject_sale_url(dapi_url + "/reject_sale");
    graft::RejectSaleResponse reject_sale_response;
    std::string reject_sale_request("{\"PaymentID\":\"" + sale_response.PaymentID + "\"}");
    res = client.serve_json_res(reject_sale_url, reject_sale_request);
    response.load(res.data(), res.length());
    reject_sale_response = response.get<graft::RejectSaleResponse>();
    EXPECT_EQ(STATUS_OK, reject_sale_response.Result);

    res = client.serve_json_res(sale_status_url, sale_status_request);
    response.load(res.data(), res.length());
    sale_status_response = response.get<graft::SaleStatusResponse>();
    EXPECT_EQ(static_cast<int>(graft::RTAStatus::RejectedByPOS), sale_status_response.Status);
}

TEST_F(GraftServerTest, testRTARejectPayFlow)
{
    graft::Input response;
    std::string res;
    Client client;

    std::string wallet_address("F4TD8JVFx2xWLeL3qwSmxLWVcPbmfUM1PanF2VPnQ7Ep2LjQCVncxqH3EZ3XCCuqQci5xi5GCYR7KRoytradoJg71DdfXpz");
    std::string details("22222222222222222222");
    std::string amount("10.0");

    std::string sale_url(dapi_url + "/sale");
    std::string correct_sale_request("{\"Address\":\"" + wallet_address
                                     + "\",\"SaleDetails\":\"" + details
                                     + "\",\"Amount\":\"" + amount + "\"}");
    res = client.serve_json_res(sale_url, correct_sale_request);
    response.load(res.data(), res.length());
    graft::SaleResponse sale_response = response.get<graft::SaleResponse>();
    EXPECT_EQ(36, sale_response.PaymentID.length());
    ASSERT_FALSE(sale_response.BlockNumber < 0); //TODO: Change to `BlockNumber <= 0`

    std::string sale_status_url(dapi_url + "/sale_status");
    graft::SaleStatusResponse sale_status_response;
    std::string sale_status_request("{\"PaymentID\":\"" + sale_response.PaymentID + "\"}");
    res = client.serve_json_res(sale_status_url, sale_status_request);
    response.load(res.data(), res.length());
    sale_status_response = response.get<graft::SaleStatusResponse>();
    EXPECT_EQ(static_cast<int>(graft::RTAStatus::Waiting), sale_status_response.Status);

    std::string sale_details_url(dapi_url + "/sale_details");
    std::string sale_details_request("{\"PaymentID\":\"" + sale_response.PaymentID + "\"}");
    res = client.serve_json_res(sale_details_url, sale_details_request);
    response.load(res.data(), res.length());
    graft::SaleDetailsResponse sale_details_response = response.get<graft::SaleDetailsResponse>();
    EXPECT_EQ(details, sale_details_response.Details);

    std::string reject_pay_url(dapi_url + "/reject_pay");
    graft::RejectPayResponse reject_pay_response;
    std::string reject_sale_request("{\"PaymentID\":\"" + sale_response.PaymentID + "\"}");
    res = client.serve_json_res(reject_pay_url, reject_sale_request);
    response.load(res.data(), res.length());
    reject_pay_response = response.get<graft::RejectPayResponse>();
    EXPECT_EQ(STATUS_OK, reject_pay_response.Result);

    std::string pay_status_url(dapi_url + "/pay_status");
    graft::PayStatusResponse pay_status_response;
    std::string pay_status_request("{\"PaymentID\":\"" + sale_response.PaymentID + "\"}");
    res = client.serve_json_res(pay_status_url, pay_status_request);
    response.load(res.data(), res.length());
    pay_status_response = response.get<graft::PayStatusResponse>();
    EXPECT_EQ(static_cast<int>(graft::RTAStatus::RejectedByWallet), pay_status_response.Status);
}

TEST_F(GraftServerTest, testRTAFailedPayment)
{
    graft::Input response;
    std::string res;
    ErrorResponse error_response;
    Client client;

    std::string sale_url(dapi_url + "/sale");
    graft::SaleResponse sale_response;
    std::string correct_sale_request("{\"Address\":\"F4TD8JVFx2xWLeL3qwSmxLWVcPbmfUM1PanF2VPnQ7Ep2LjQCVncxqH3EZ3XCCuqQci5xi5GCYR7KRoytradoJg71DdfXpz\",\"SaleDetails\":\"dddd\",\"Amount\":\"10.0\"}");
    res = client.serve_json_res(sale_url, correct_sale_request);
    response.load(res.data(), res.length());
    sale_response = response.get<graft::SaleResponse>();
    EXPECT_EQ(36, sale_response.PaymentID.length());
    ASSERT_FALSE(sale_response.BlockNumber < 0); //TODO: Change to `BlockNumber <= 0`

    std::string reject_sale_url(dapi_url + "/reject_sale");
    graft::RejectSaleResponse reject_sale_response;
    std::string reject_sale_request("{\"PaymentID\":\"" + sale_response.PaymentID + "\"}");
    res = client.serve_json_res(reject_sale_url, reject_sale_request);
    response.load(res.data(), res.length());
    reject_sale_response = response.get<graft::RejectSaleResponse>();
    EXPECT_EQ(STATUS_OK, reject_sale_response.Result);

    std::string sale_details_url(dapi_url + "/sale_details");
    std::string sale_details_request("{\"PaymentID\":\"" + sale_response.PaymentID + "\"}");
    res = client.serve_json_res(sale_details_url, sale_details_request);
    response.load(res.data(), res.length());
    error_response = response.get<ErrorResponse>();
    EXPECT_EQ(ERROR_RTA_FAILED, error_response.code);
    EXPECT_EQ(MESSAGE_RTA_FAILED, error_response.message);

    std::string pay_url(dapi_url + "/pay");
    std::string wallet_address("F4TD8JVFx2xWLeL3qwSmxLWVcPbmfUM1PanF2VPnQ7Ep2LjQCVncxqH3EZ3XCCuqQci5xi5GCYR7KRoytradoJg71DdfXpz");
    std::string amount("10.0");
    std::string pay_request("{\"Address\":\"" + wallet_address
                            + "\",\"BlockNumber\":0,\"PaymentID\":\""
                            + sale_response.PaymentID
                            + "\",\"Amount\":\"" + amount + "\"}");
    res = client.serve_json_res(pay_url, pay_request);
    response.load(res.data(), res.length());
    error_response = response.get<ErrorResponse>();
    EXPECT_EQ(ERROR_RTA_FAILED, error_response.code);
    EXPECT_EQ(MESSAGE_RTA_FAILED, error_response.message);
}

TEST_F(GraftServerTest, testRTACompletedPayment)
{
    graft::Input response;
    std::string res;
    ErrorResponse error_response;
    Client client;

    std::string wallet_address("F4TD8JVFx2xWLeL3qwSmxLWVcPbmfUM1PanF2VPnQ7Ep2LjQCVncxqH3EZ3XCCuqQci5xi5GCYR7KRoytradoJg71DdfXpz");
    std::string details("22222222222222222222");
    std::string amount("10.0");

    std::string sale_url(dapi_url + "/sale");
    std::string correct_sale_request("{\"Address\":\"" + wallet_address
                                     + "\",\"SaleDetails\":\"" + details
                                     + "\",\"Amount\":\"" + amount + "\"}");
    res = client.serve_json_res(sale_url, correct_sale_request);
    response.load(res.data(), res.length());
    graft::SaleResponse sale_response = response.get<graft::SaleResponse>();
    EXPECT_EQ(36, sale_response.PaymentID.length());
    ASSERT_FALSE(sale_response.BlockNumber < 0); //TODO: Change to `BlockNumber <= 0`

    std::string pay_url(dapi_url + "/pay");
    std::string pay_request("{\"Address\":\"" + wallet_address
                            + "\",\"BlockNumbe\":0,\"PaymentID\":\""
                            + sale_response.PaymentID
                            + "\",\"Amount\":\"" + amount + "\"}");
    res = client.serve_json_res(pay_url, pay_request);
    response.load(res.data(), res.length());
    graft::PayResponse pay_response = response.get<graft::PayResponse>();
    EXPECT_EQ(STATUS_OK, pay_response.Result);

    std::string sale_details_url(dapi_url + "/sale_details");
    std::string sale_details_request("{\"PaymentID\":\"" + sale_response.PaymentID + "\"}");
    res = client.serve_json_res(sale_details_url, sale_details_request);
    response.load(res.data(), res.length());
    error_response = response.get<ErrorResponse>();
    EXPECT_EQ(ERROR_RTA_COMPLETED, error_response.code);
    EXPECT_EQ(MESSAGE_RTA_COMPLETED, error_response.message);

    res = client.serve_json_res(pay_url, pay_request);
    response.load(res.data(), res.length());
    error_response = response.get<ErrorResponse>();
    EXPECT_EQ(ERROR_RTA_COMPLETED, error_response.code);
    EXPECT_EQ(MESSAGE_RTA_COMPLETED, error_response.message);
}

TEST_F(GraftServerTest, testRTAFullFlow)
{
    graft::Input response;
    std::string res;
    Client client;

    std::string wallet_address("F4TD8JVFx2xWLeL3qwSmxLWVcPbmfUM1PanF2VPnQ7Ep2LjQCVncxqH3EZ3XCCuqQci5xi5GCYR7KRoytradoJg71DdfXpz");
    std::string details("22222222222222222222");
    std::string amount("10.0");

    std::string sale_url(dapi_url + "/sale");
    std::string correct_sale_request("{\"Address\":\"" + wallet_address
                                     + "\",\"SaleDetails\":\"" + details
                                     + "\",\"Amount\":\"" + amount + "\"}");
    res = client.serve_json_res(sale_url, correct_sale_request);
    response.load(res.data(), res.length());
    graft::SaleResponse sale_response = response.get<graft::SaleResponse>();
    EXPECT_EQ(36, sale_response.PaymentID.length());
    ASSERT_FALSE(sale_response.BlockNumber < 0); //TODO: Change to `BlockNumber <= 0`

    std::string sale_status_url(dapi_url + "/sale_status");
    std::string sale_status_request("{\"PaymentID\":\"" + sale_response.PaymentID + "\"}");
    res = client.serve_json_res(sale_status_url, sale_status_request);
    response.load(res.data(), res.length());
    graft::SaleStatusResponse sale_status_response = response.get<graft::SaleStatusResponse>();
    EXPECT_EQ(static_cast<int>(graft::RTAStatus::Waiting), sale_status_response.Status);

    std::string sale_details_url(dapi_url + "/sale_details");
    std::string sale_details_request("{\"PaymentID\":\"" + sale_response.PaymentID + "\"}");
    res = client.serve_json_res(sale_details_url, sale_details_request);
    response.load(res.data(), res.length());
    graft::SaleDetailsResponse sale_details_response = response.get<graft::SaleDetailsResponse>();
    EXPECT_EQ(details, sale_details_response.Details);

    std::string pay_url(dapi_url + "/pay");
    std::string pay_request("{\"Address\":\"" + wallet_address
                            + "\",\"BlockNumber\":0,\"PaymentID\":\""
                            + sale_response.PaymentID
                            + "\",\"Amount\":\"" + amount + "\"}");
    res = client.serve_json_res(pay_url, pay_request);
    response.load(res.data(), res.length());
    graft::PayResponse pay_response = response.get<graft::PayResponse>();
    EXPECT_EQ(STATUS_OK, pay_response.Result);

    std::string pay_status_url(dapi_url + "/pay_status");
    graft::PayStatusResponse pay_status_response;
    std::string pay_status_request("{\"PaymentID\":\"" + sale_response.PaymentID + "\"}");
    res = client.serve_json_res(pay_status_url, pay_status_request);
    response.load(res.data(), res.length());
    pay_status_response = response.get<graft::PayStatusResponse>();
    EXPECT_EQ(static_cast<int>(graft::RTAStatus::Success), pay_status_response.Status);

    res = client.serve_json_res(sale_status_url, sale_status_request);
    response.load(res.data(), res.length());
    sale_status_response = response.get<graft::SaleStatusResponse>();
    EXPECT_EQ(static_cast<int>(graft::RTAStatus::Success), sale_status_response.Status);
}
