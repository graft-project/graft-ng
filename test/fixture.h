
#pragma once

#include "lib/graft/connection.h"
#include "lib/graft/mongoosex.h"
#include "lib/graft/context.h"
#include "supernode/server.h"
#include "test.h"

#include <gtest/gtest.h>
#include <thread>
#include <atomic>

namespace detail
{

class GSTest : public graft::GraftServer
{
public:
    GSTest(graft::Router& httpRouter, bool ignoreInitConfig)
        : m_httpRouter(httpRouter)
        , m_ignoreInitConfig(ignoreInitConfig)
    { }
    bool ready() const { return graft::GraftServer::ready(); }
    void stop() { graft::GraftServer::stop(); }
    graft::GlobalContextMap& getContext() { return graft::GraftServer::getLooper().getGcm(); }
    graft::Looper& getLooper() { return graft::GraftServer::getLooper(); }

protected:
    virtual bool initConfigOption(int argc, const char** argv, graft::ConfigOpts& configOpts) override
    {
        if(m_ignoreInitConfig) return true; //prevents loading parameters from command line and config.ini
        return graft::GraftServer::initConfigOption(argc, argv, configOpts);
    }
    virtual void initRouters() override
    {
        graft::ConnectionManager* httpcm = getConMgr("HTTP");
        httpcm->addRouter(m_httpRouter);
    }
private:
    graft::Router& m_httpRouter;
    bool m_ignoreInitConfig;
};

}//namespace detail

/////////////////////////////////
// GraftServerTestBase fixture
//
//It has:
//1. class MainServer based on GraftServer
//2. class Client based on mongoose
//3. internal class TempCryptoNodeServer to simulate upstream behavior

class GraftServerTestBase : public ::testing::Test
{
protected:
    //Server to simulate CryptoNode (its object is created in non-main thread)
    class TempCryptoNodeServer
    {
    public:
        std::string port = "1234";
        int connect_timeout_ms = 1000;
        int poll_timeout_ms = 1000;
        bool keepAlive = false;

        using on_http_t = bool (const http_message *hm, int& status_code, std::string& headers, std::string& data);
        std::function<on_http_t> on_http = nullptr;
        static std::function<on_http_t> http_echo;

        void run()
        {
            ready = false;
            stop = false;
            th = std::thread([this]{ x_run(); });
            while(!ready)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }
        void stop_and_wait_for()
        {
            stop = true;
            th.join();
        }
    protected:
        virtual bool onHttpRequest(const http_message *hm, int& status_code, std::string& headers, std::string& data)
        {
            assert(on_http);
            return on_http(hm, status_code, headers, data);
        }
        virtual void onClose() { }
    private:
        std::thread th;
        std::atomic_bool ready;
        std::atomic_bool stop;

        void x_run()
        {
            mg_mgr mgr;
            mg_mgr_init(&mgr, this, 0);
            mg_connection *nc = mg_bind(&mgr, port.c_str(), ev_handler_http_s);
            mg_set_protocol_http_websocket(nc);
            ready = true;
            for (;;) {
                mg_mgr_poll(&mgr, poll_timeout_ms);
                if(stop) break;
            }
            mg_mgr_free(&mgr);
        }

        static void ev_handler_empty_s(mg_connection *client, int ev, void *ev_data)
        {
        }
        static void ev_handler_http_s(mg_connection *client, int ev, void *ev_data)
        {
            TempCryptoNodeServer* This = static_cast<TempCryptoNodeServer*>(client->mgr->user_data);
            assert(dynamic_cast<TempCryptoNodeServer*>(This));
            This->ev_handler_http(client, ev, ev_data);
        }
        void ev_handler_http(mg_connection *client, int ev, void *ev_data)
        {
            switch(ev)
            {
            case MG_EV_HTTP_REQUEST:
            {
                mg_set_timer(client, 0);
                struct http_message *hm = (struct http_message *) ev_data;
                int status_code = 200;
                std::string headers, data;
                bool res = onHttpRequest(hm, status_code, headers, data);
                if(!res) break;
                mg_send_head(client, status_code, data.size(), headers.c_str());
                mg_send(client, data.c_str(), data.size());
                if(!keepAlive)
                {
                    client->flags |= MG_F_SEND_AND_CLOSE;
                }
            } break;
            case MG_EV_CLOSE:
            {
                onClose();
            } break;
            case MG_EV_ACCEPT:
            {
                mg_set_timer(client, mg_time() + connect_timeout_ms);
            } break;
            case MG_EV_TIMER:
            {
                mg_set_timer(client, 0);
                client->handler = ev_handler_empty_s; //without this we will get MG_EV_HTTP_REQUEST
                client->flags |= MG_F_CLOSE_IMMEDIATELY;
             } break;
            default:
                break;
            }
        }
    };

public:
    class MainServer
    {
    public:
        graft::ConfigOpts m_copts;
        graft::Router m_router;

        graft::Looper& getLooper() const { assert(m_gserver); return m_gserver->getLooper(); }
        graft::GlobalContextMap& getGcm() const { assert(m_gserver); return m_gserver->getContext(); }

        MainServer()
        {
            m_copts.http_address = "127.0.0.1:9084";
            m_copts.coap_address = "127.0.0.1:9086";
            m_copts.http_connection_timeout = 1;
            m_copts.upstream_request_timeout = 1;
            m_copts.workers_count = 0;
            m_copts.worker_queue_len = 0;
            m_copts.workers_expelling_interval_ms = 1000;
            m_copts.cryptonode_rpc_address = "127.0.0.1:1234";
            m_copts.timer_poll_interval_ms = 50;
            m_copts.lru_timeout_ms = 60000;
        }

        void run()
        {
            m_th = std::thread([this]{ x_run(); });
            while(!m_gserver_created || !m_gserver->ready())
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }
        void stop_and_wait_for()
        {
            m_gserver->stop();
            m_th.join();
            m_gserver_created = false;
            m_gserver.reset();
        }

    private:
        std::atomic_bool m_gserver_created{false};
        std::unique_ptr<detail::GSTest> m_gserver{nullptr};
        std::thread m_th;

        void x_run()
        {
            m_gserver = std::make_unique<detail::GSTest>(m_router, true);
            m_gserver_created = true;
            m_gserver->init(start_args.argc, start_args.argv, m_copts);
            m_gserver->run();
        }
    };
public:
    //http client (its objects are created in the main thread)
    class Client
    {
    public:
        int timeout_ms = 0;

    public:
        Client()
        {
            mg_mgr_init(&m_mgr, nullptr, nullptr);
        }

        void serve(const std::string& url, const std::string& extra_headers = std::string(), const std::string& post_data = std::string(), int timeout_ms = 0, int poll_time_ms = 0)
        {
            m_exit = false; m_closed = false;
            client = mg_connect_http(&m_mgr, graft::static_ev_handler<Client>, url.c_str(),
                                     (extra_headers.empty())? nullptr : extra_headers.c_str(),
                                     (post_data.empty())? nullptr : post_data.c_str()); //last nullptr means GET
            assert(client);
            client->user_data = this;

            if(0 < timeout_ms && poll_time_ms == 0)
            {
                poll_time_ms = timeout_ms/4;
                if(poll_time_ms == 0) ++poll_time_ms;
            }
            if(poll_time_ms == 0) poll_time_ms = 1000;

            auto end = std::chrono::steady_clock::now()
                    + std::chrono::duration<int,std::milli>(timeout_ms);
            while(!m_exit)
            {
                mg_mgr_poll(&m_mgr, poll_time_ms);
                if(0 < timeout_ms && end <= std::chrono::steady_clock::now())
                {
                    client->flags |= MG_F_CLOSE_IMMEDIATELY;
                }
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
        int get_resp_code(){ return m_resp_code; }

        ~Client()
        {
            mg_mgr_free(&m_mgr);
        }
    public:
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
                m_resp_code = hm->resp_code;
                m_body = std::string(hm->body.p, hm->body.len);
                client->flags |= MG_F_CLOSE_IMMEDIATELY;
                client->handler = graft::static_empty_ev_handler;
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
                client->handler = graft::static_empty_ev_handler;
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
        int m_resp_code = 0;
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
        s << "curl ";
        if(!json_data.empty())
            s << "--data \"" << json_data << "\" ";
        s << url;
        return run_cmdline_read(s.str());
    }

protected:
    virtual void SetUp() override
    { }
    virtual void TearDown() override
    { }

};

/////////////////////////////////
// GraftServerTest fixture
//
//Typical usage:
//1. set m_copts fields to specific values required for a test.
//   And set m_ignoreConfigIni to true, otherwise the options will be read from config.ini
//2. register required endpoints using m_httpRouter
//3. run(); - launches the server
//4. stop_and_wait_for(); - shutdowns the server

class GraftServerTest : public ::testing::Test
{
public:
    bool m_ignoreConfigIni = true;
    graft::ConfigOpts m_copts;
    graft::Router m_httpRouter;

    void run()
    {
        m_th = std::thread([this]
        {
            m_gserver = std::make_unique<detail::GSTest>(m_httpRouter, m_ignoreConfigIni);
            m_serverCreated = true;
            m_gserver->init(start_args.argc, start_args.argv, m_copts);
            m_gserver->run();
        });
        while(!m_serverCreated || !m_gserver->ready())
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    void stop_and_wait_for()
    {
        m_gserver->stop();
        m_th.join();
    }

private:
    std::unique_ptr<detail::GSTest> m_gserver;
    std::atomic_bool m_serverCreated{false};
    std::thread m_th;
private:
    void initOpts()
    {
        m_copts.http_address = "0.0.0.0:28690";
        m_copts.coap_address = "udp://0.0.0.0:18991";
        m_copts.workers_count = 0;
        m_copts.worker_queue_len = 0;
        m_copts.http_connection_timeout = 360;
        m_copts.timer_poll_interval_ms = 1000;
        m_copts.upstream_request_timeout = 360;
        m_copts.cryptonode_rpc_address = "127.0.0.1:28681";
        m_copts.graftlet_dirs.emplace_back("graftlets");
        m_copts.lru_timeout_ms = 60000;
        m_copts.workers_expelling_interval_ms = 1000;
    }
protected:
    GraftServerTest()
    {
        initOpts();
    }

    ~GraftServerTest()
    {
    }
protected:
    static void SetUpTestCase()
    {
    }

    static void TearDownTestCase()
    {
    }
};
