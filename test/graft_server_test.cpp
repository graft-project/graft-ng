#include <gtest/gtest.h>
#include "context.h"
#include "graft_manager.h"
#include "inout.h"

TEST(InOut, common)
{
	using namespace graft;

	auto f = [](const Input& in, Output& out)
	{
		EXPECT_EQ(in.get(), "abc");
		out.load("def");
	};

	char buffer[] = { 'a', 'b', 'c' };
	graft::InString in;
	graft::OutString  out;

	in.load(&buffer[0], 3);
	in.load(&buffer[0], 3);
	f(in, out);

	EXPECT_EQ(in.get(), "abc");
	auto t = out.get();
	EXPECT_EQ(std::string(t.first, t.second), "def");
}

TEST(Context, common)
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
// GraftServerTest fixture

class GraftServerTest : public ::testing::Test
{
public:
	static std::string iocheck;
	static std::deque<graft::Router::Status> res_que_peri;
	static graft::Router::Handler3 h3_test;
	static std::thread t_CN;
	static std::thread t_srv;
	static bool run_server_ready;

	const std::string uri_base = "http://localhost:9084";
	const std::string uri = "/root/r55";

private:
	//Server to simulate CryptoNode (its object is created in non-main thread)
	class TempCryptoNodeServer
	{
	public:
		static void run()
		{
			mg_mgr mgr;
			mg_mgr_init(&mgr, NULL, 0);
			mg_connection *nc = mg_bind(&mgr, "1234", ev_handler);
			ready = true;
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
				std::string data;
				bool ok = graft::CryptoNodeSender::help_recv_pstring(client, ev_data, data);
				if(!ok) break;
				EXPECT_EQ(data, iocheck);
				data += '4';
				iocheck = data;

				graft::CryptoNodeSender::help_send_pstring(client, data);
				client->flags |= MG_F_SEND_AND_CLOSE;
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
		assert(h3_test.peri);
		graft::Router router;
		{
			static graft::Router::Handler3 p(h3_test);
			router.addRoute("/root/r{id:\\d+}", METHOD_GET, &p);
			router.addRoute("/root/r{id:\\d+}", METHOD_POST, &p);
			router.addRoute("/root/aaa/{s1}/bbb/{s2}", METHOD_GET, &p);
			bool res = router.arm();
			EXPECT_EQ(res, true);
		}
		graft::Manager manager(router);

		manager.initThreadPool();
		graft::GraftServer gs;
		gs.serve(manager.get_mg_mgr(),"9084");
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

protected:
	static void SetUpTestCase()
	{
		auto pre = [&](const graft::Router::vars_t& vars, const graft::Input& input, graft::Output& output)->graft::Router::Status
		{
			std::string in = input.get();
			EXPECT_EQ(in, iocheck);
			iocheck = in + '1';
			output.load(iocheck);
			return graft::Router::Status::Ok;
		};
		auto peri = [&](const graft::Router::vars_t& vars, const graft::Input& input, graft::Output& output)->graft::Router::Status
		{
			std::string in = input.get();
			EXPECT_EQ(in, iocheck);
			graft::Router::Status res = graft::Router::Status::Ok;
			if(!res_que_peri.empty())
			{
				res = res_que_peri.front();
				res_que_peri.pop_front();
			}
			iocheck = in + '2';
			output.load(iocheck);
			return res;
		};
		auto post = [&](const graft::Router::vars_t& vars, const graft::Input& input, graft::Output& output)->graft::Router::Status
		{
			std::string in = input.get();
			EXPECT_EQ(in, iocheck);
			iocheck = in + '3';
			output.load(iocheck);
			return graft::Router::Status::Ok;
		};

		h3_test = graft::Router::Handler3(graft::Router::Handler(pre), graft::Router::Handler(peri), graft::Router::Handler(post));

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
std::deque<graft::Router::Status> GraftServerTest::res_que_peri;
graft::Router::Handler3 GraftServerTest::h3_test;
std::thread GraftServerTest::t_CN;
std::thread GraftServerTest::t_srv;
bool GraftServerTest::run_server_ready = false;

bool GraftServerTest::TempCryptoNodeServer::ready = false;
bool GraftServerTest::TempCryptoNodeServer::stop = false;


TEST_F(GraftServerTest, GETtp)
{//GET -> threadPool
	iocheck = "";
	Client client;
	client.serve((uri_base+uri).c_str());
	std::string res = client.get_body();
	EXPECT_EQ("Job done.", res);
	EXPECT_EQ("123", iocheck);
}

TEST_F(GraftServerTest, GETtpCNtp)
{//GET -> threadPool -> CryptoNode -> threadPool
	iocheck = "";
	res_que_peri.clear();
	res_que_peri.push_back(graft::Router::Status::Forward);
	res_que_peri.push_back(graft::Router::Status::Ok);
	Client client;
	client.serve((uri_base+uri).c_str());
	std::string res = client.get_body();
	EXPECT_EQ("Job done.", res);
	EXPECT_EQ("1234123", iocheck);
}

TEST_F(GraftServerTest, clPOSTtp)
{//POST cmdline -> threadPool
	std::string body = "input body";
	iocheck = body;
	{
		std::ostringstream s;
		s << "curl --data \"" << body << "\" " << (uri_base+uri);
		std::string ss = s.str();
		std::string res = run_cmdline_read(ss.c_str());
		EXPECT_EQ("Job done.", res);
		EXPECT_EQ(body + "123", iocheck);
	}
}

TEST_F(GraftServerTest, clPOSTtpCNtp)
{//POST cmdline -> threadPool -> CryptoNode -> threadPool
	std::string body = "input body";
	iocheck = body;
	res_que_peri.clear();
	res_que_peri.push_back(graft::Router::Status::Forward);
	res_que_peri.push_back(graft::Router::Status::Ok);
	{
		std::ostringstream s;
		s << "curl --data \"" << body << "\" " << (uri_base+uri);
		std::string ss = s.str();
		std::string res = run_cmdline_read(ss.c_str());
		EXPECT_EQ("Job done.", res);
		EXPECT_EQ(body + "1234123", iocheck);
	}
}

/* TODO: crash on this
		client.serve((uri_base+uri).c_str(),
					 "Content-Type: text/plain\r\n",
					 body.c_str());
*/
