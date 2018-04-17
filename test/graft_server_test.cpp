#include <gtest/gtest.h>
#include "context.h"
#include "graft_manager.h"

TEST(context, common)
{
	graft::Context ctx;
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
}

/////////////////////////////////
// graft_server tests

//Server to simulate CryptoNode
class TempCryptoNodeServer
{
public:
	static void run()
	{
		mg_mgr mgr;
		mg_mgr_init(&mgr, NULL, 0);
		mg_connection *nc = mg_bind(&mgr, "1234", ev_handler);
		for (;;) {
		  mg_mgr_poll(&mgr, 1000);
		  if(stop) break;
		}
		mg_mgr_free(&mgr);
	}
private:
	static void ev_handler(mg_connection *client, int ev, void *ev_data)
	{
		switch (ev)
		{
		case MG_EV_RECV:
		{
			int m_cnt = *(int*)ev_data;
			if(m_cnt<100) break;
			mbuf& buf = client->recv_mbuf;
			static std::string data = std::string(buf.buf, buf.len);
			{
				static bool b = false;
				data[0] = b? '0' : '1';
				b = !b;
			}
			mg_send(client, data.c_str(), data.size());
			client->flags |= MG_F_SEND_AND_CLOSE;
		} break;
		default:
		  break;
		}
	}
public:
	static bool stop;
};

bool TempCryptoNodeServer::stop = false;

graft::Router::Status test(const graft::Router::vars_t& vars, const std::string& input, std::string& output)
{
	static int b = 0;
	return (++b%3)? graft::Router::Status::Forward : graft::Router::Status::Ok;
}

void run_server()
{
	graft::Router router;
	{
		static graft::Router::Handler p = test;
		router.addRoute("/root/r{id:\\d+}", METHOD_GET, &p);
		router.addRoute("/root/aaa/{s1}/bbb/{s2}", METHOD_GET, &p);
		bool res = router.arm();
		EXPECT_EQ(res, true);
	}
	graft::Manager manager(router);

	manager.initThreadPool();
	graft::GraftServer gs;
	gs.serve(manager.get_mg_mgr(),"9084");
}

class Client : public graft::StaticMongooseHandler<Client>
{
public:
	Client()
	{
		mg_mgr_init(&m_mgr, nullptr, nullptr);
	}

	void serve(const char* url, const char* extra_headers = nullptr, const char* post_data = nullptr)
	{
		client = mg_connect_http(&m_mgr, static_ev_handler, url, extra_headers, post_data); //last NULL means GET
		assert(client);
		client->user_data = this;
		while(!m_exit)
		{
			mg_mgr_poll(&m_mgr, 1000);
		}
	}

	std::string getResultBody(){ return m_body; }
	std::string getResultMessage(){ return m_message; }

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
			m_exit = true;
		} break;
		}
	}
private:
	bool m_exit = false;
	mg_mgr m_mgr;
	mg_connection* client;
	std::string m_body;
	std::string m_message;
};


TEST(graft_server, common)
{
	std::thread t_CN([]{ TempCryptoNodeServer::run(); });
	std::thread t_srv([]{ run_server(); });

	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	{
		Client client;
		client.serve("http://localhost:9084/root/r55");
		std::string res = client.getResultBody();
		EXPECT_STREQ("Job done.", res.c_str());
	}
	{
		Client client;
		client.serve("http://localhost:9084/root/exit");
	}
	TempCryptoNodeServer::stop = true;

	t_srv.join();
	t_CN.join();
}
