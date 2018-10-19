#include <gtest/gtest.h>
#include <thread>
#include <misc_log_ex.h>
#include "mongoosex.h"
#include "connection.h"
#include "fixture.h"

class ServerWS : public LooperTestBase
{
public:
    class Client
    {
        struct mg_mgr mgr;
        struct mg_connection *nc = nullptr;
        const char *ws_addr = "ws://127.0.0.1:9090/ws";
        bool client_done;
        bool s_is_connected = false;
    public:
        void serve()
        {
            client_done = false;

            mg_mgr_init(&mgr, 0, 0);

            nc = mg_connect_ws(&mgr, graft::static_ev_handler<Client>, ws_addr, "my_protocol", nullptr); // client_coap_handler);
            nc->user_data = this;
//            mg_set_protocol_coap(nc);

            while (!client_done) {
              mg_mgr_poll(&mgr, 1000000);
            }

            mg_mgr_free(&mgr);
        }

        void ev_handler(struct mg_connection *nc, int ev, void *ev_data)
        {
            struct websocket_message *wm = (struct websocket_message *) ev_data;
            switch (ev) {
              case MG_EV_CONNECT: {
                LOG_PRINT_L0("MG_EV_CONNECT client");
                int status = *((int *) ev_data);
                if (status != 0) {
                  printf("-- Connection error: %d\n", status);
                }
                break;
              }
              case MG_EV_WEBSOCKET_HANDSHAKE_DONE: {
                LOG_PRINT_L0("MG_EV_WEBSOCKET_HANDSHAKE_DONE client");
                printf("-- Connected\n");
                s_is_connected = 1;
                mg_set_timer(nc, mg_time() + 5);
                break;
              }
            case MG_EV_TIMER:
            {
                LOG_PRINT_L0("MG_EV_TIMER client");
                std::string s = "client send something";
                mg_send_websocket_frame(nc, WEBSOCKET_OP_TEXT, s.c_str(), s.size());
                static int cnt = 10;
                if(0 < --cnt)  mg_set_timer(nc, mg_time() + 5);
                else
                {
                    nc->flags |= MG_F_SEND_AND_CLOSE;
                }
            } break;
              case MG_EV_POLL: {
                LOG_PRINT_L0("MG_EV_POLL client");
//                mg_send_websocket_frame(nc, WEBSOCKET_OP_TEXT, msg, n);
                std::string s = "client send something";
//                mg_send_websocket_frame(nc, WEBSOCKET_OP_TEXT, s.c_str(), s.size());
                break;
              }
              case MG_EV_WEBSOCKET_FRAME: {
/*
                LOG_PRINT_L0("MG_EV_WEBSOCKET_FRAME client");
                printf("%.*s\n", (int) wm->size, wm->data);
                struct websocket_message *wm = (struct websocket_message *) ev_data;
*/
                std::string msg((char *) wm->data, wm->size);
  //              broadcast(nc, d);
                LOG_PRINT_L0("MG_EV_WEBSOCKET_FRAME client : ") << msg;
                break;
              }
              case MG_EV_CLOSE: {
                LOG_PRINT_L0("MG_EV_CLOSE client");
                if (s_is_connected) printf("-- Disconnected\n");
                client_done = 1;
                break;
              }
            }
        }

        bool ready = false;
        bool stop = false;
    };
public:
    ServerWS() {}
private:
//    Server server;
//    std::thread th;
};

TEST_F(ServerWS, Common)
{
    MainServer server;
    server.run();

    Client client;
    client.serve();

    server.stop_and_wait_for();
}

extern "C"
{

struct mg_http_endpoint;
struct mg_http_endpoint *mg_http_get_endpoint_handler(struct mg_connection *nc,
                                                      struct mg_str *uri_path);
} //extern "C"

class WS  : public ::testing::Test
{
public:
    static const char *ws_port;
public:
    class Server
    {
        struct mg_mgr mgr;
        struct mg_connection *nc = nullptr;
    public:
        void serve()
        {

            mg_mgr_init(&mgr, 0, 0);

            nc = mg_bind(&mgr, ws_port, graft::static_ev_handler<Server>);
            nc->user_data = this;
            mg_set_protocol_http_websocket(nc);

            ready = true;
            while (!stop)
            {
              mg_mgr_poll(&mgr, 1000);
            }

            mg_mgr_free(&mgr);
        }

        void ev_handler(struct mg_connection *nc, int ev, void *ev_data)
        {
            static mg_str* last_uri = nullptr;

            websocket_message* wm = (websocket_message*) ev_data;
            switch (ev) {
            case MG_EV_CONNECT:
            {
                LOG_PRINT_L0("MG_EV_CONNECT server");
            } break;
            case MG_EV_WEBSOCKET_HANDSHAKE_REQUEST:
            {
                http_message* hm = (http_message*) ev_data;
                mg_str& uri = hm->uri;
                last_uri = &hm->uri;
                LOG_PRINT_L0("MG_EV_WEBSOCKET_HANDSHAKE_REQUEST server uri = ") << std::string(uri.p, uri.len);
            } break;
            case MG_EV_WEBSOCKET_HANDSHAKE_DONE:
            {
                //it is expected that MG_EV_WEBSOCKET_HANDSHAKE_DONE follows MG_EV_WEBSOCKET_HANDSHAKE_REQUEST immediately, and the last one knows request uri
                assert(last_uri);
                mg_str& uri = *last_uri; // mg_mk_str(nullptr);
//                mg_http_get_endpoint_handler(nc->listener, &uri);
                LOG_PRINT_L0("MG_EV_WEBSOCKET_HANDSHAKE_DONE server uri = ") << std::string(uri.p, uri.len);
              /* New websocket connection. Tell everybody. */
             // broadcast(nc, mg_mk_str("++ joined"));
                mg_set_timer(nc, mg_time() + 7);
            } break;
            case MG_EV_TIMER:
            {
                LOG_PRINT_L0("MG_EV_TIMER client");
                std::string s = "server send something";
                mg_send_websocket_frame(nc, WEBSOCKET_OP_TEXT, s.c_str(), s.size());
                mg_set_timer(nc, mg_time() + 7);
            } break;
            case MG_EV_WEBSOCKET_FRAME:
            {
              struct websocket_message *wm = (struct websocket_message *) ev_data;
              /* New websocket message. Tell everybody. */
              struct mg_str d = {(char *) wm->data, wm->size};
              std::string msg((char *) wm->data, wm->size);
//              broadcast(nc, d);
              LOG_PRINT_L0("MG_EV_WEBSOCKET_FRAME server : ") << msg;
            } break;
            case MG_EV_HTTP_REQUEST:
            {
                LOG_PRINT_L0("MG_EV_HTTP_REQUEST server");
//              mg_serve_http(nc, (struct http_message *) ev_data, s_http_server_opts);
                mg_http_send_error(nc, 200, "OK");
            } break;
            case MG_EV_CLOSE:
            {
                LOG_PRINT_L0("MG_EV_CLOSE server");
              /* Disconnect. Tell everybody. */
/*
              if (is_websocket(nc)) {
                broadcast(nc, mg_mk_str("-- left"));
              }
*/
              break;
            }
          }
        }

        bool ready = false;
        bool stop = false;
    };

    void run()
    {
        th = std::thread([this]{ server.serve(); });
        while(!server.ready)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    void stop_and_wait_for()
    {
        server.stop = true;
        th.join();
    }
public:
    class Client
    {
        struct mg_mgr mgr;
        struct mg_connection *nc = nullptr;
        const char *ws_addr = "ws://127.0.0.1:8000/aaaBBB";
        bool client_done;
        bool s_is_connected = false;
    public:
        void serve()
        {
            client_done = false;

            mg_mgr_init(&mgr, 0, 0);

            nc = mg_connect_ws(&mgr, graft::static_ev_handler<Client>, ws_addr, "my_protocol", nullptr); // client_coap_handler);
            nc->user_data = this;
//            mg_set_protocol_coap(nc);

            while (!client_done) {
              mg_mgr_poll(&mgr, 1000000);
            }

            mg_mgr_free(&mgr);
        }

        void ev_handler(struct mg_connection *nc, int ev, void *ev_data)
        {
            struct websocket_message *wm = (struct websocket_message *) ev_data;
            switch (ev) {
              case MG_EV_CONNECT: {
                LOG_PRINT_L0("MG_EV_CONNECT client");
                int status = *((int *) ev_data);
                if (status != 0) {
                  printf("-- Connection error: %d\n", status);
                }
                break;
              }
              case MG_EV_WEBSOCKET_HANDSHAKE_DONE: {
                LOG_PRINT_L0("MG_EV_WEBSOCKET_HANDSHAKE_DONE client");
                printf("-- Connected\n");
                s_is_connected = 1;
                mg_set_timer(nc, mg_time() + 5);
                break;
              }
            case MG_EV_TIMER:
            {
                LOG_PRINT_L0("MG_EV_TIMER client");
                std::string s = "client send something";
                mg_send_websocket_frame(nc, WEBSOCKET_OP_TEXT, s.c_str(), s.size());
                static int cnt = 10;
                if(0 < --cnt)  mg_set_timer(nc, mg_time() + 5);
                else
                {
                    nc->flags |= MG_F_SEND_AND_CLOSE;
                }
            } break;
              case MG_EV_POLL: {
                LOG_PRINT_L0("MG_EV_POLL client");
//                mg_send_websocket_frame(nc, WEBSOCKET_OP_TEXT, msg, n);
                std::string s = "client send something";
//                mg_send_websocket_frame(nc, WEBSOCKET_OP_TEXT, s.c_str(), s.size());
                break;
              }
              case MG_EV_WEBSOCKET_FRAME: {
/*
                LOG_PRINT_L0("MG_EV_WEBSOCKET_FRAME client");
                printf("%.*s\n", (int) wm->size, wm->data);
                struct websocket_message *wm = (struct websocket_message *) ev_data;
*/
                std::string msg((char *) wm->data, wm->size);
  //              broadcast(nc, d);
                LOG_PRINT_L0("MG_EV_WEBSOCKET_FRAME client : ") << msg;
                break;
              }
              case MG_EV_CLOSE: {
                LOG_PRINT_L0("MG_EV_CLOSE client");
                if (s_is_connected) printf("-- Disconnected\n");
                client_done = 1;
                break;
              }
            }
        }

        bool ready = false;
        bool stop = false;
    };
public:
    WS() {}
private:
    Server server;
    std::thread th;
};

const char* WS::ws_port = "8000";

TEST_F(WS, clientServer)
{
    run();

    Client client;
    client.serve();

    stop_and_wait_for();
}

////////////////////////////////////////////////////////
static bool client_done = false;

/*
static void client_coap_handler(struct mg_connection *nc, int ev, void *p) {
  switch (ev) {
    case MG_EV_TIMER:
      std::cout << "\ntimout\n";
/ *
      static int cnt = 0;
      if(++cnt < 1) break;
* /
    case MG_EV_CONNECT: {
      struct mg_coap_message cm;

      memset(&cm, 0, sizeof(cm));
      cm.msg_id = 1;
      cm.msg_type = MG_COAP_MSG_CON;
      LOG_PRINT_L2("coap send");
      mg_coap_send_message(nc, &cm);
      if(ev!=MG_EV_TIMER) mg_set_timer(nc, mg_time() + 10);
      break;
    }
    case MG_EV_COAP_ACK:
    case MG_EV_COAP_RST: {
      struct mg_coap_message *cm = (struct mg_coap_message *) p;
      printf("ACK/RST for message with msg_id = %d received\n", cm->msg_id);
      client_done = 1;
      break;
    }
    case MG_EV_CLOSE: {
      if (client_done == 0) {
        printf("Server closed connection\n");
        client_done = 1;
      }
      break;
    }
  }
}
*/

/*
static void server_coap_handler(struct mg_connection *nc, int ev, void *p) {
  switch (ev) {
    case MG_EV_COAP_CON: {
      uint32_t res;
      struct mg_coap_message *cm = (struct mg_coap_message *) p;
      printf("CON with msg_id = %d received\n", cm->msg_id);
      res = mg_coap_send_ack(nc, cm->msg_id);
      if (res == 0) {
        printf("Successfully sent ACK for message with msg_id = %d\n",
               cm->msg_id);
      } else {
        printf("Error: %d\n", res);
      }
      break;
    }
    case MG_EV_COAP_NOC:
    case MG_EV_COAP_ACK:
    case MG_EV_COAP_RST: {
      struct mg_coap_message *cm = (struct mg_coap_message *) p;
      printf("ACK/RST/NOC with msg_id = %d received\n", cm->msg_id);
      break;
    }
  }
}
*/

static void server_coap_handler(struct mg_connection *nc, int ev, void *p) {
  switch (ev) {
    case MG_EV_COAP_CON: {
      uint32_t res;
      struct mg_coap_message *cm = (struct mg_coap_message *) p;
      printf("CON with msg_id = %d received\n", cm->msg_id);

      //ignore first time
      static bool first = true;
      if(first)
      {
          first = false;
          break;
      }

      res = mg_coap_send_ack(nc, cm->msg_id);
      if (res == 0) {
        printf("Successfully sent ACK for message with msg_id = %d\n",
               cm->msg_id);
      } else {
        printf("Error: %d\n", res);
      }
      break;
    }
    case MG_EV_COAP_NOC:
    case MG_EV_COAP_ACK:
    case MG_EV_COAP_RST: {
      struct mg_coap_message *cm = (struct mg_coap_message *) p;
      printf("ACK/RST/NOC with msg_id = %d received\n", cm->msg_id);
      break;
    }
  }
}

struct CoapParams
{
    int ack_timout_ms = 2000;
    double ack_random_factor = 1.5;
    int max_retransmit = 4;


};

class Coap  : public ::testing::Test
{
public:
    static const char *coap_addr;
public:
    class Server
    {
        struct mg_mgr mgr;
        struct mg_connection *nc = nullptr;
    public:
        void serve()
        {

            mg_mgr_init(&mgr, 0, 0);

            nc = mg_bind(&mgr, coap_addr, server_coap_handler);
            mg_set_protocol_coap(nc);

            ready = true;
            while (!stop)
            {
              mg_mgr_poll(&mgr, 1000);
            }

            mg_mgr_free(&mgr);
        }

        bool ready = false;
        bool stop = false;
    };

    void run()
    {
        th = std::thread([this]{ server.serve(); });
        while(!server.ready)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    void stop_and_wait_for()
    {
        server.stop = true;
        th.join();
    }
public:
    class Client
    {
        struct mg_mgr mgr;
        struct mg_connection *nc = nullptr;
        const char *coap_addr = "udp://:5683";
    public:
        void serve()
        {
            client_done = false;

            mg_mgr_init(&mgr, 0, 0);

            nc = mg_connect(&mgr, coap_addr, graft::static_ev_handler<Client>); // client_coap_handler);
            nc->user_data = this;
            mg_set_protocol_coap(nc);

            while (!client_done) {
              mg_mgr_poll(&mgr, 1000000);
            }

            mg_mgr_free(&mgr);
        }

        static void ev_handler(struct mg_connection *nc, int ev, void *p)
        {
          switch (ev) {
            case MG_EV_TIMER:
              std::cout << "\ntimout\n";
        /*
              static int cnt = 0;
              if(++cnt < 1) break;
        */
            case MG_EV_CONNECT: {
              struct mg_coap_message cm;

              memset(&cm, 0, sizeof(cm));
              cm.msg_id = 1;
              cm.msg_type = MG_COAP_MSG_CON;
              LOG_PRINT_L2("coap send");
              mg_coap_send_message(nc, &cm);
              if(ev!=MG_EV_TIMER) mg_set_timer(nc, mg_time() + 10);
              break;
            }
            case MG_EV_COAP_ACK:
            case MG_EV_COAP_RST: {
              struct mg_coap_message *cm = (struct mg_coap_message *) p;
              printf("ACK/RST for message with msg_id = %d received\n", cm->msg_id);
              client_done = 1;
              break;
            }
            case MG_EV_CLOSE: {
              if (client_done == 0) {
                printf("Server closed connection\n");
                client_done = 1;
              }
              break;
            }
          }
        }

        bool ready = false;
        bool stop = false;
    };
public:
    Coap() {}
private:
    Server server;
    std::thread th;
};

const char* Coap::coap_addr = "udp://:5683";


TEST_F(Coap, clientServer)
{
    run();

    Client client;
    client.serve();

    stop_and_wait_for();
}

