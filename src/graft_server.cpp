#include "mongoose.h"
#include "router.h"
#include "thread_pool.h"

namespace graft {

class Manager;

class ClientRequest;
using ClientRequest_ptr = std::shared_ptr<ClientRequest>;

class CryptoNodeSender;

class GJ_ptr;
using TPResQueue = tp::MPMCBoundedQueue< GJ_ptr >;

using Router = RouterT<std::string, std::string>;

using GJ = GraftJob<ClientRequest_ptr, Router::JobParams, TPResQueue, Manager, std::string>;

//////////////
/// \brief The GJ_ptr class
/// A wrapper of GraftJob that will be moved from queue to queue with fixed size.
/// It contains single data member of unique_ptr
///
class GJ_ptr final
{
	std::unique_ptr<GJ> m_ptr = nullptr;
public:
	GJ_ptr(GJ_ptr&& rhs)
	{
		*this = std::move(rhs);
	}
	GJ_ptr& operator = (GJ_ptr&& rhs)
	{
		//identity check (this != &rhs) is not required, it will be done in the next copy assignment
		m_ptr = std::move(rhs.m_ptr);
		return * this;
	}

	explicit GJ_ptr() = default;
	GJ_ptr(const GJ_ptr&) = delete;
	GJ_ptr& operator = (const GJ_ptr&) = delete;
	~GJ_ptr() = default;

	template<typename ...ARGS>
	GJ_ptr(ARGS&&... args) : m_ptr( new GJ( std::forward<ARGS>(args)...) )
	{
	}

	template<typename ...ARGS>
	void operator ()(ARGS... args)
	{
		m_ptr.get()->operator () (args...);
	}

	GJ* operator ->()
	{
		return m_ptr.operator ->();
	}

	GJ& operator *()
	{
		return m_ptr.operator *();
	}
};

using ThreadPoolX = tp::ThreadPoolImpl<tp::FixedFunction<void(), sizeof(GJ_ptr)>,
								  tp::MPMCBoundedQueue>;

///////////////////////////////////

class Manager
{
public:
	Manager(Router& router)
		: m_router(router)
	{
		mg_mgr_init(&m_mgr, this, cb_event);
	}

	void setThreadPool(ThreadPoolX&& tp, TPResQueue&& rq, uint64_t m_threadPoolInputSize_)
	{
		m_threadPool = std::unique_ptr<ThreadPoolX>(new ThreadPoolX(std::move(tp)));
		m_resQueue = std::unique_ptr<TPResQueue>(new TPResQueue(std::move(rq)));
		m_threadPoolInputSize = m_threadPoolInputSize_;
	}

	void notifyJobReady()
	{
		mg_notify(&m_mgr);
	}

	void doWork(uint64_t cnt);

	void sendCrypton(ClientRequest_ptr cr);
	void sendToThreadPool(ClientRequest_ptr cr);

	////getters
	mg_mgr* get_mg_mgr() { return &m_mgr; }
	Router& get_Router() { return m_router; }
	ThreadPoolX& get_threadPool() { return *m_threadPool.get(); }
	TPResQueue& get_resQueue() { return *m_resQueue.get(); }

	////static functions
	static void cb_event(mg_mgr* mgr, uint64_t cnt)
	{
		Manager::from(mgr)->doWork(cnt);
	}

	static Manager* from(mg_mgr* mgr)
	{
		assert(mgr->user_data);
		return static_cast<Manager*>(mgr->user_data);
	}

	static Manager* from(mg_connection* cn)
	{
		return from(cn->mgr);
	}

	////events
	void onNewClient(ClientRequest_ptr cr)
	{
		++m_cntClientRequest;
		static bool b = false;
		if(b)
		{
			sendCrypton(cr);
		}
		else
		{
			sendToThreadPool(cr);
		}
		b = !b;
	}
	void onClientDone(ClientRequest_ptr cr)
	{
		++m_cntClientRequestDone;
	}

	void onJobDone(GJ& gj);
	
	void onCryptonDone(CryptoNodeSender& cns);

private:
	bool tryProcessReadyJob();
	void processReadyJobBlock();
private:
	mg_mgr m_mgr;
	Router& m_router;

	uint64_t m_cntClientRequest = 0;
	uint64_t m_cntClientRequestDone = 0;
	uint64_t m_cntCryptoNodeSender = 0;
	uint64_t m_cntCryptoNodeSenderDone = 0;
	uint64_t m_cntJobSent = 0;
	uint64_t m_cntJobDone = 0;

	uint64_t m_threadPoolInputSize = 0;
	std::unique_ptr<ThreadPoolX> m_threadPool;
	std::unique_ptr<TPResQueue> m_resQueue;
public:
	bool exit = false;
};

template<typename C>
class StaticMongooseHandler
{
public:
	static void static_ev_handler(mg_connection *nc, int ev, void *ev_data) 
	{
		static bool entered = false;
		assert(!entered); //recursive calls are dangerous
		entered = true;
		C* This = static_cast<C*>(nc->user_data);
		assert(This);
		This->ev_handler(nc, ev, ev_data);
		entered = false;
	}
protected:
	static void static_empty_ev_handler(mg_connection *nc, int ev, void *ev_data)
	{

	}
};

template<typename C>
class ItselfHolder
{
public:	
	using Ptr = std::shared_ptr<C>;
public:	
	Ptr get_Itself() { return m_itself; }
	
	template<typename ...ARGS>
	static const Ptr Create(ARGS&&... args)
	{
		return (new C(std::forward<ARGS>(args)...))->m_itself;
	}
	void releaseItself() { m_itself.reset(); }
protected:
	ItselfHolder() : m_itself(static_cast<C*>(this)) { }
private:
	Ptr m_itself;
};

class CryptoNodeSender : public ItselfHolder<CryptoNodeSender>, StaticMongooseHandler<CryptoNodeSender>
{
public:	
	CryptoNodeSender() = default;
	
	ClientRequest_ptr& get_CR() { return m_cr; }

	void send(Manager& manager, ClientRequest_ptr cr, std::string& data)
	{
		m_cr = cr;
		m_data = data;
		m_crypton = mg_connect(manager.get_mg_mgr(),"localhost:1234", static_ev_handler);
		m_crypton->user_data = this;
		mg_send(m_crypton, m_data.c_str(), m_data.size());
	}
private:
	friend class StaticMongooseHandler<CryptoNodeSender>;
	void ev_handler(mg_connection* crypton, int ev, void *ev_data) 
	{
		assert(crypton == this->m_crypton);
		switch (ev) 
		{
		case MG_EV_RECV:
		{
			int m_cnt = *(int*)ev_data;
			if(m_cnt<100) break;
			mbuf& buf = crypton->recv_mbuf;
			m_result = std::string(buf.buf, buf.len);
			crypton->flags |= MG_F_CLOSE_IMMEDIATELY;
			Manager::from(crypton)->onCryptonDone(*this);
			crypton->handler = static_empty_ev_handler;
			releaseItself();
		} break;
		default:
		  break;
		}
	}
private:
	mg_connection *m_crypton = nullptr;
	ClientRequest_ptr m_cr;
	std::string m_data;
	std::string m_result;
};

class ClientRequest : public ItselfHolder<ClientRequest>, public StaticMongooseHandler<ClientRequest>
{
private:
	friend class ItselfHolder<ClientRequest>;
	ClientRequest(mg_connection *client, Router::JobParams& prms) 
		: m_prms(prms)
		, m_client(client)
	{
	}
public:	
	void respondToClientAndDie(const std::string& s)
	{
		mg_send(m_client, s.c_str(), s.size());
		m_client->flags |= MG_F_SEND_AND_CLOSE;
		m_client->handler = static_empty_ev_handler;
		releaseItself();
	}
	
	void createJob(Manager& manager)
	{
		manager.get_threadPool().post(
			GJ_ptr( get_Itself(), Router::JobParams(m_prms), &manager.get_resQueue(), &manager )
		);
	}

	void onJobDone(GJ& gj)
	{
		//here you can send a request to cryptonode or send response to client
		//gj will be destroyed on exit, save its result
		//now it sends response to client
		respondToClientAndDie("Job done");
	}
	
	void onCryptonDone(CryptoNodeSender& cns)
	{
		//here you can send a job to the thread pool or send response to client
		//cns will be destroyed on exit, save its result
		//now it sends response to client
		respondToClientAndDie("I am answering Ok");
	}
private:
	friend class StaticMongooseHandler<ClientRequest>;
	void ev_handler(mg_connection *client, int ev, void *ev_data) 
	{
		assert(client == this->m_client);
		switch (ev) 
		{
		case MG_EV_CLOSE:
		{
			assert(get_Itself());
			if(get_Itself()) break;
			Manager::from(client)->onClientDone(get_Itself());
			client->handler = static_empty_ev_handler;
			releaseItself();
		} break;
		default:
		  break;
		}
	}
private:
	Router::JobParams m_prms;
	mg_connection *m_client;
};

class GraftServer final
{
public:	
	void serve(mg_mgr* mgr, const char* s_http_port)
	{
		mg_connection* nc = mg_bind(mgr, s_http_port, ev_handler);
		mg_set_protocol_http_websocket(nc);
		for (;;) 
		{
			mg_mgr_poll(mgr, 1000);
			if(Manager::from(mgr)->exit) break;
		}
		mg_mgr_free(mgr);
	}

private:
	static void ev_handler(mg_connection *client, int ev, void *ev_data) 
	{
		switch (ev) 
		{
		case MG_EV_HTTP_REQUEST:
		{
			struct http_message *hm = (struct http_message *) ev_data;
			std::string uri(hm->uri.p, hm->uri.len);
			Manager* manager = Manager::from(client);
			if(uri == "/root/exit")
			{
				manager->exit = true;
				return;
			}
			std::string s_method(hm->method.p, hm->method.len);
			int method = (s_method == "GET")? METHOD_GET: 1;

			Router& router = manager->get_Router();
			Router::JobParams prms;
			if(router.match(uri, method, prms))
			{
				ClientRequest* ptr = ClientRequest::Create(client, prms).get();
				client->user_data = ptr;
				client->handler = ClientRequest::static_ev_handler;
				Manager::from(client)->onNewClient( ptr->get_Itself() );
			}
			else
			{
				mg_http_send_error(client, 500, "invalid parameter");
				client->flags |= MG_F_SEND_AND_CLOSE;
			}
		} break;
		default:
		  break;
		}
	}
};

void Manager::sendCrypton(ClientRequest_ptr cr)
{
	++m_cntCryptoNodeSender;
	CryptoNodeSender::Ptr cns = CryptoNodeSender::Create();
	std::string something(100, ' ');
	{
		std::string s("something");
		for(int i=0; i< s.size(); ++i)
		{
			something[i] = s[i];
		}
	}
	cns->send(*this, cr, something );
}

void Manager::sendToThreadPool(ClientRequest_ptr cr)
{
	assert(m_cntJobDone <= m_cntJobSent);
	if(m_cntJobDone - m_cntJobSent == m_threadPoolInputSize)
	{//check overflow
		processReadyJobBlock();
	}
	assert(m_cntJobDone - m_cntJobSent < m_threadPoolInputSize);
	++m_cntJobSent;
	cr->createJob(*this);
}

bool Manager::tryProcessReadyJob()
{
	GJ_ptr gj;
	bool res = get_resQueue().pop(gj);
	if(!res) return res;
	onJobDone(*gj);
	return true;
}

void Manager::processReadyJobBlock()
{
	while(true)
	{
		bool res = tryProcessReadyJob();
		if(res) break;
	}
}

void Manager::doWork(uint64_t m_cnt)
{
	//job overflow is possible, and we can pop jobs without notification, thus m_cnt useless
	bool res = true;
	while(res)
	{
		res = tryProcessReadyJob();
		if(!res) break;
	}
}

void Manager::onJobDone(GJ& gj)
{
	gj.get_CR()->onJobDone(gj);
	++m_cntJobDone;
	//gj will be destroyed on exit
}

void Manager::onCryptonDone(CryptoNodeSender& cns)
{
	cns.get_CR()->onCryptonDone(cns);
	++m_cntCryptoNodeSenderDone;
	//cns will be destroyed on exit
}

}//namespace graft

//Temporary server to simulate CryptoNode
class TempCryptoNodeServer
{
public:
	static void run(graft::Manager& extern_manager)
	{
		mg_mgr mgr;
		mg_mgr_init(&mgr, NULL, 0);
		mg_connection *nc = mg_bind(&mgr, "1234", ev_handler);
		for (;;) {
		  mg_mgr_poll(&mgr, 1000);
		  if(extern_manager.exit) break;
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
			mg_send(client, data.c_str(), data.size());
			client->flags |= MG_F_SEND_AND_CLOSE;
		} break;
		default:
		  break;
		}
	}
};

bool test(const graft::Router::vars_t& vars, const std::string& input, std::string& output)
{
	return true;
}

void init_threadPool(graft::Manager& manager)
{
	tp::ThreadPoolOptions th_op;
	th_op.setQueueSize(32);
	graft::ThreadPoolX thread_pool(th_op);

	size_t resQueueSize;
	{//nearest ceiling power of 2
		size_t val = th_op.threadCount()*th_op.queueSize();
		size_t bit = 1;
		for(; bit<val; bit <<= 1);
		resQueueSize = bit;
	}

	const size_t maxinputSize = th_op.threadCount()*th_op.queueSize();
	assert(maxinputSize == th_op.threadCount()*th_op.queueSize());
	graft::TPResQueue resQueue(resQueueSize);
	
	manager.setThreadPool(std::move(thread_pool), std::move(resQueue), maxinputSize);
}

int main(int argc, char *argv[]) 
{

	graft::Router router;
	{
		static graft::Router::Handler p = test;
		router.addRoute("/root/r{id:\\d+}", METHOD_GET, &p);
		router.addRoute("/root/aaa/{s1}/bbb/{s2}", METHOD_GET, &p);
		bool res = router.arm();
		assert(res);
	}
	graft::Manager manager(router);

	std::thread t([&manager]{ TempCryptoNodeServer::run(manager); });

	init_threadPool(manager);
	graft::GraftServer gs;
	gs.serve(manager.get_mg_mgr(),"9084");
	
	t.join();
	return 0;
}

