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
//	std::string& c = ctx.local[key];

//	std::cout << bb << " " << c << std::endl;
//	std::cout << c << std::endl;
	ctx.local[key[0]] = std::string("bbbbb");

	const std::string& b = ctx.local[key[0]];
	std::cout << b << " " << bb << " " << std::string(cc.begin(), cc.end()) << std::endl;
#endif
	ctx.global[key[0]] = s;
	ctx.global[key[1]] = v;
	std::string bbg = ctx.global[key[0]];
	const std::vector<char> ccg = ctx.global[key[1]];
//	std::string& c = ctx.global[key];

//	std::cout << bb << " " << c << std::endl;
//	std::cout << c << std::endl;
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
				iocheck = data += '4'; skip_ctx_check = true;

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

		auto pre = [&](const graft::Router::vars_t& vars, const graft::Input& input, graft::Context& ctx, graft::Output& output)->graft::Router::Status
		{
			std::string in = input.get();
			EXPECT_EQ(in, iocheck);
			check_ctx(ctx, in);
			iocheck = in + '1';
			output.load(iocheck);
			ctx.global[iocheck] = iocheck;
			ctx.local[iocheck] = iocheck;
			return graft::Router::Status::Ok;
		};
		auto peri = [&](const graft::Router::vars_t& vars, const graft::Input& input, graft::Context& ctx, graft::Output& output)->graft::Router::Status
		{
			std::string in = input.get();
			EXPECT_EQ(in, iocheck);
			check_ctx(ctx, in);
			graft::Router::Status res = graft::Router::Status::Ok;
			if(!res_que_peri.empty())
			{
				res = res_que_peri.front();
				res_que_peri.pop_front();
			}
			iocheck = in + '2';
			output.load(iocheck);
			ctx.global[iocheck] = iocheck;
			ctx.local[iocheck] = iocheck;
			return res;
		};
		auto post = [&](const graft::Router::vars_t& vars, const graft::Input& input, graft::Context& ctx, graft::Output& output)->graft::Router::Status
		{
			std::string in = input.get();
			EXPECT_EQ(in, iocheck);
			check_ctx(ctx, in);
			iocheck = in + '3';
			output.load(iocheck);
			ctx.global[iocheck] = iocheck;
			ctx.local[iocheck] = iocheck;
			return graft::Router::Status::Ok;
		};

		h3_test = graft::Router::Handler3(pre, peri, post);

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
std::deque<graft::Router::Status> GraftServerTest::res_que_peri;
graft::Router::Handler3 GraftServerTest::h3_test;
std::thread GraftServerTest::t_CN;
std::thread GraftServerTest::t_srv;
bool GraftServerTest::run_server_ready = false;

bool GraftServerTest::TempCryptoNodeServer::ready = false;
bool GraftServerTest::TempCryptoNodeServer::stop = false;


TEST_F(GraftServerTest, GETtp)
{//GET -> threadPool
	iocheck = ""; skip_ctx_check = true;
	Client client;
	client.serve((uri_base+uri).c_str());
	std::string res = client.get_body();
	EXPECT_EQ("Job done.", res);
	EXPECT_EQ("123", iocheck);
}

TEST_F(GraftServerTest, GETtpCNtp)
{//GET -> threadPool -> CryptoNode -> threadPool
	iocheck = ""; skip_ctx_check = true;
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
	iocheck = body; skip_ctx_check = true;
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
	iocheck = body; skip_ctx_check = true;
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
