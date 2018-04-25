#pragma once

#include "mongoose.h"
#include "router.h"
#include "thread_pool.h"
#include "inout.h"
#include "context.h"

#include "CMakeConfig.h"

namespace graft {

class Manager;

class ClientRequest;
using ClientRequest_ptr = std::shared_ptr<ClientRequest>;

class CryptoNodeSender;

class GJ_ptr;
using TPResQueue = tp::MPMCBoundedQueue< GJ_ptr >;

using Router = RouterT<Input, Output>;

using GJ = GraftJob<ClientRequest_ptr, TPResQueue, Manager>;

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

    void initThreadPool(int threadCount = std::thread::hardware_concurrency(), int workersQueueSize = 32);

    void notifyJobReady();

    void doWork(uint64_t cnt);

    void sendCrypton(ClientRequest_ptr cr);
    void sendToThreadPool(ClientRequest_ptr cr);

    ////getters
    mg_mgr* get_mg_mgr() { return &m_mgr; }
    Router& get_router() { return m_router; }
    ThreadPoolX& get_threadPool() { return *m_threadPool.get(); }
    TPResQueue& get_resQueue() { return *m_resQueue.get(); }
    GlobalContextMap& get_gcm() { return m_gcm; }

    ////static functions
    static void cb_event(mg_mgr* mgr, uint64_t cnt);

    static Manager* from(mg_mgr* mgr);

    static Manager* from(mg_connection* cn);

    ////events
    void onNewClient(ClientRequest_ptr cr);
    void onClientDone(ClientRequest_ptr cr);

    void onJobDone(GJ& gj);

    void onCryptonDone(CryptoNodeSender& cns);

private:
    void setThreadPool(ThreadPoolX&& tp, TPResQueue&& rq, uint64_t m_threadPoolInputSize_);

    bool tryProcessReadyJob();
    void processReadyJobBlock();
private:
    mg_mgr m_mgr;
    Router& m_router;
    GlobalContextMap m_gcm;

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
    Ptr get_itself() { return m_itself; }

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

    ClientRequest_ptr& get_cr() { return m_cr; }

    void send(Manager& manager, ClientRequest_ptr cr, const std::string& data);
public:
    const Output& get_result() { return m_result; }
public:
    static void help_send_pstring(mg_connection *nc, const std::string& data);
    static bool help_recv_pstring(mg_connection *nc, void *ev_data, std::string& data);
private:
    friend class StaticMongooseHandler<CryptoNodeSender>;
    void ev_handler(mg_connection* crypton, int ev, void *ev_data);
private:
    mg_connection *m_crypton = nullptr;
    ClientRequest_ptr m_cr;
    std::string m_data;
    Output m_result;
};

class ClientRequest : public ItselfHolder<ClientRequest>, public StaticMongooseHandler<ClientRequest>
{
private:
    friend class ItselfHolder<ClientRequest>;
    ClientRequest(mg_connection *client, Router::JobParams& prms, GlobalContextMap& gcm)
        : m_prms(prms)
        , m_client(client)
        , m_ctx(gcm)
    {
    }
public:
    void respondToClientAndDie(const std::string& s);

    void createJob(Manager& manager);

    void onJobDone(GJ& gj);

    void onCryptonDone(CryptoNodeSender& cns);
public:
    Router::Status& get_statusRef() { return m_status; }
    const Router::vars_t& get_vars() const { return m_prms.vars; }
    const Input& get_input() const { return m_prms.input; }
    Output& get_output() { return m_output; }
    const Router::Handler3& get_h3() const { return m_prms.h3; }
    Context& get_ctx() { return m_ctx; }
private:
    friend class StaticMongooseHandler<ClientRequest>;
    void ev_handler(mg_connection *client, int ev, void *ev_data);
private:
    Router::Status m_status = Router::Status::None;
    Router::JobParams m_prms;
    Output m_output;
    mg_connection *m_client;
    Context m_ctx;
};

class GraftServer final
{
#ifdef OPT_BUILD_TESTS
public:
    static std::atomic_bool ready;
#endif
public:
    void serve(mg_mgr* mgr, const char* s_http_port);
    /**
   * @brief setCryptonodeRpcAddress - setup cryptonode RPC address
   * @param address - address in IP:port form
   */
    void setCryptonodeRPCAddress(const std::string &address);
    /**
   * @brief setCryptonodeP2PAddress - setup cryptonode P2P address
   * @param address - address in IP:port form
   */
    void setCryptonodeP2PAddress(const std::string &address);

private:
    static void ev_handler(mg_connection *client, int ev, void *ev_data);
};

}//namespace graft

