
#include <gtest/gtest.h>
#include <chrono>

#include <boost/program_options.hpp>
#include <boost/property_tree/ini_parser.hpp>

#include "connection.h"
#include "mongoosex.h"
#include "server.h"

class Client
{
  public:
    Client(void);
    ~Client(void);

    void ev_handler(mg_connection* client, int ev, void* ev_data);
    void serve(const std::string& url, const std::string& extra_headers = std::string(),
      const std::string& post_data = std::string(), int timeout_ms = 0);

    std::string serve_json_res(const std::string& url, const std::string& json_data);

    bool get_closed(){ return closed_; }
    std::string get_body(){ return body_; }
    std::string get_message(){ return message_; }
    int get_resp_code(){ return resp_code_; }

  private:
    void on_mg_connect(void* ev_data);
    void on_mg_http_reply(mg_connection* client, void* ev_data);
    void on_mg_recv(mg_connection* client, void* ev_data);
    void on_mg_close(mg_connection* client);

  private:
    bool exit_;
    bool closed_;
    mg_mgr mgr_;
    mg_connection* client_;
    int resp_code_;
    std::string body_;
    std::string message_;
};

Client::Client(void)
: exit_(false)
, closed_(false)
, client_(nullptr)
, resp_code_(0)
{
  mg_mgr_init(&mgr_, nullptr, nullptr);
}

Client::~Client(void)
{
  mg_mgr_free(&mgr_);
}

std::string Client::serve_json_res(const std::string& url, const std::string& json_data)
{
  serve(url, "Content-Type: application/json\r\n", json_data);
  EXPECT_EQ(false, get_closed());
  return get_body();
}

void Client::serve(const std::string& url, const std::string& extra_headers, const std::string& post_data, int timeout_ms)
{
  exit_ = false;
  closed_ = false;
  client_ = mg_connect_http(&mgr_, graft::static_ev_handler<Client>, url.c_str(),
                            extra_headers.empty() ? nullptr : extra_headers.c_str(),
                            post_data.empty() ? nullptr : post_data.c_str()); //last nullptr means GET
  assert(client_);
  client_->user_data = this;

  int poll_time_ms = 1000;
  if(0 < timeout_ms)
  {
    poll_time_ms = timeout_ms / 4;
    if(poll_time_ms == 0) ++poll_time_ms;
  }
  auto end = std::chrono::steady_clock::now()
          + std::chrono::duration<int,std::milli>(timeout_ms);
  while(!exit_)
  {
    mg_mgr_poll(&mgr_, poll_time_ms);
    if(0 < timeout_ms && end <= std::chrono::steady_clock::now())
    {
      client_->flags |= MG_F_CLOSE_IMMEDIATELY;
    }
  }
}

void Client::ev_handler(mg_connection* client, int ev, void* ev_data)
{
  assert(client == this->client_);
  switch(ev)
  {
    case MG_EV_CONNECT:     on_mg_connect(ev_data); break;
    case MG_EV_HTTP_REPLY:  on_mg_http_reply(client, ev_data); break;
    case MG_EV_RECV:        on_mg_recv(client, ev_data); break;
    case MG_EV_CLOSE:       on_mg_close(client); break;
  }
}

void Client::on_mg_connect(void* ev_data)
{
  const int& err = *static_cast<int*>(ev_data);
  if(err != 0)
  {
    std::ostringstream s;
    s << "connect() failed: " << strerror(err);
    message_ = s.str();
    exit_ = true;
  }
}

void Client::on_mg_http_reply(mg_connection* client, void* ev_data)
{
  const http_message* hm = static_cast<http_message*>(ev_data);
  resp_code_ = hm->resp_code;
  body_ = std::string(hm->body.p, hm->body.len);
  client->flags |= MG_F_CLOSE_IMMEDIATELY;
  client->handler = graft::static_empty_ev_handler;
  exit_ = true;
}

void Client::on_mg_recv(mg_connection* client, void* ev_data)
{
  int cnt = *static_cast<int*>(ev_data);
  mbuf& buf = client->recv_mbuf;
  message_ = std::string(buf.buf, buf.len);
}

void Client::on_mg_close(mg_connection* client)
{
  client->handler = graft::static_empty_ev_handler;
  closed_ = true;
  exit_ = true;
}

namespace po = boost::program_options;
namespace pt = boost::property_tree;

class GraftServerForTest : public graft::GraftServer
{
  private:
    //void override_config_values(pt::ptree& ini_data, po::variables_map& cmdline_data) override;
};

//void GraftServerForTest::override_config_values(pt::ptree& ini_data, po::variables_map& cmdline_data)
//{
//  std::cout << "GraftServerForTest::override_config_values called" << std::endl;
//  ini_data.put("", 0);
//}

TEST(Supernode, RequestSystemInfo)
{
  GraftServerForTest srv;
  const int argc = 1;
  //const char* argv[] = {"1", "2", "3", "4"};
  const char* argv[] = {"./graft_server"};
  srv.run(argc, argv);
  std::cout << "srv.running ...." << std::endl;

  Client client;
  client.serve("http://127.0.0.1:9084/timeout", "", "post data", 200);
  //graft::Context ctx(mainServer.plooper.load()->getGcm());
  //ctx.global["method"] = METHOD_GET;
  //ctx.global["requestPath"] = std::string("0");
  //iocheck = "0"; skip_ctx_check = true;
  //Client client;
  //client.serve(uri_base+"r1");
  //EXPECT_EQ(false, client.get_closed());
  //std::string res = client.get_body();
  //EXPECT_EQ("0123", iocheck);
  EXPECT_EQ(true, true);
}

class TestForNGRTA103 : public ::testing::Test
{
  public:
    TestForNGRTA103(void)
    : pgs_(nullptr)
    , srv_running_(false)
    {
    }

    ~TestForNGRTA103(void)
    {
    }

    void server_thread_func(void)
    {
      graft::GraftServer grft_srv;
      pgs_ = &grft_srv;
      const int argc = 1;
      const char* argv[] = {"./graft_server"};
      srv_running_ = true;
      grft_srv.run(argc, argv);
    }

    void run_supernode_server(void)
    {
      server_thread_ = std::thread([this]{ server_thread_func(); });

      //while(!pgs || !pgs_.load()->ready())
      //  std::this_thread::sleep_for(std::chrono::milliseconds(10));


    }

  private:
  public:
    std::atomic<graft::GraftServer*> pgs_;
    std::atomic<bool> srv_running_;
    std::thread server_thread_;

};

TEST_F(TestForNGRTA103, body)
{
  //auto server_thread =
  //
  pgs_ = nullptr;

  EXPECT_EQ(true, true);
}



