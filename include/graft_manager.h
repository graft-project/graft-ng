#pragma once

#include <list>

#include "mongoosex.h"
#include "router.h"
#include "thread_pool.h"
#include "inout.h"
#include "context.h"
#include "timer.h"
#include <misc_log_ex.h>

#include "CMakeConfig.h"

#define LOG_PRINT_CLN(level,client,x) LOG_PRINT_L##level("[" << inet_ntoa(client->sa.sin.sin_addr) << ":" << ntohs(client->sa.sin.sin_port) << "]" << x)

#define LOG_PRINT_RQS(level,x) \
{ \
    ClientRequest* cr = dynamic_cast<ClientRequest*>(get_itself().get()); \
    if(cr) \
    { \
        LOG_PRINT_CLN(level,cr->get_client(),x); \
    } \
    else \
    { \
        LOG_PRINT_L##level(x); \
    } \
}

namespace graft {

class Manager;

class BaseTask;
using BaseTask_ptr = std::shared_ptr<BaseTask>;

class CryptoNodeSender;

class GJ_ptr;
using TPResQueue = tp::MPMCBoundedQueue< GJ_ptr >;
using GJ = GraftJob<BaseTask_ptr, TPResQueue, Manager>;

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

using ThreadPoolX = tp::ThreadPoolImpl<tp::FixedFunction<void(), sizeof(GJ_ptr)>, tp::MPMCBoundedQueue>;

///////////////////////////////////

struct ServerOpts
{
    std::string http_address;
    std::string coap_address;
    double http_connection_timeout;
    double upstream_request_timeout;
    int workers_count;
    int worker_queue_len;
    std::string cryptonode_rpc_address;
    int timer_poll_interval_ms;
    // data directory - where
    std::string data_dir;
};

class Manager
{
public:
    Manager(const ServerOpts& sopts)
        : m_sopts(sopts)
    {
        // TODO: validate options, throw exception if any mandatory options missing
        mg_mgr_init(&m_mgr, this, cb_event);
        initThreadPool(sopts.workers_count, sopts.worker_queue_len);
    }
    ~Manager();

    void notifyJobReady();

    void doWork(uint64_t cnt);

    void sendCrypton(BaseTask_ptr cr);
    void sendToThreadPool(BaseTask_ptr cr);

    void addRouter(Router& r) { m_root.addRouter(r); }

    bool enableRouting() { return m_root.arm(); }
    bool matchRoute(const std::string& target, int method, Router::JobParams& params) { return m_root.match(target, method, params); }

    void addPeriodicTask(const Router::Handler3& h3, std::chrono::milliseconds interval_ms);

    ////getters
    mg_mgr* get_mg_mgr() { return &m_mgr; }
    ThreadPoolX& get_threadPool() { return *m_threadPool.get(); }
    TPResQueue& get_resQueue() { return *m_resQueue.get(); }
    GlobalContextMap& get_gcm() { return m_gcm; }
    const ServerOpts& get_c_opts() const { return m_sopts; }
    TimerList<BaseTask_ptr>& get_timerList() { return m_timerList; }

    ////static functions
    static void cb_event(mg_mgr* mgr, uint64_t cnt);

    static Manager* from(mg_mgr* mgr);

    static Manager* from(mg_connection* cn);

    ////events
    void onNewClient(BaseTask_ptr cr);
    void onClientDone(BaseTask_ptr cr);

    void onJobDone();
    void onJobDone(GJ& gj);

    void onCryptonDone(CryptoNodeSender& cns);
    void stop();
    bool stopped() const;

private:
    void initThreadPool(int threadCount = std::thread::hardware_concurrency(), int workersQueueSize = 32);
    void setThreadPool(ThreadPoolX&& tp, TPResQueue&& rq, uint64_t m_threadPoolInputSize_);

    bool tryProcessReadyJob();
    void processReadyJobBlock();
public:
    std::string dbgDumpRouters() const { return m_root.dbgDumpRouters(); }
    void dbgDumpR3Tree(int level = 0) const { return m_root.dbgDumpR3Tree(level); }
    //returns conflicting endpoint
    std::string dbgCheckConflictRoutes() const { return m_root.dbgCheckConflictRoutes(); }
private:
    mg_mgr m_mgr;
    Router::Root m_root;
    GlobalContextMap m_gcm;

    uint64_t m_cntBaseTask = 0;
    uint64_t m_cntBaseTaskDone = 0;
    uint64_t m_cntCryptoNodeSender = 0;
    uint64_t m_cntCryptoNodeSenderDone = 0;
    uint64_t m_cntJobSent = 0;
    uint64_t m_cntJobDone = 0;

    uint64_t m_threadPoolInputSize = 0;
    std::unique_ptr<ThreadPoolX> m_threadPool;
    std::unique_ptr<TPResQueue> m_resQueue;

    ServerOpts m_sopts;
    TimerList<BaseTask_ptr> m_timerList;
public:
    volatile std::atomic_bool m_exit {false};
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

    template<typename T=C, typename ...ARGS>
    static const Ptr Create(ARGS&&... args)
    {
        return (new T(std::forward<ARGS>(args)...))->m_itself;
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

    BaseTask_ptr& get_cr() { return m_cr; }

    void send(Manager& manager, BaseTask_ptr cr);
    Status getStatus() const { return m_status; }
    const std::string& getError() const { return m_error; }
private:
    friend class StaticMongooseHandler<CryptoNodeSender>;
    void ev_handler(mg_connection* crypton, int ev, void *ev_data);
    void setError(Status status, const std::string& error = std::string())
    {
        m_status = status;
        m_error = error;
    }
private:
    mg_connection *m_crypton = nullptr;
    BaseTask_ptr m_cr;
    Status m_status = Status::None;
    std::string m_error;
};

class BaseTask : public ItselfHolder<BaseTask>
{
protected:
    friend class ItselfHolder<BaseTask>;
    BaseTask(Manager& manager, const Router::JobParams& prms)
        : m_manager(manager)
        , m_prms(prms)
        , m_ctx(manager.get_gcm())
    {
    }
public:
    virtual ~BaseTask() { }

    virtual void onEvent() { }

    void createJob();
public:
    void onJobDone(GJ* gj = nullptr); //gj equals nullptr if threadPool was skipped for some reasons
    void onCryptonDone(CryptoNodeSender& cns);
    void onTooBusy(); //called on the thread pool overflow
protected:
    virtual void respondAndDie(const std::string& s) = 0;
    void processResult();
    void setLastStatus(Status status) { Context::LocalFriend::setLastStatus(m_ctx.local, status); }
    Status getLastStatus() const { return m_ctx.local.getLastStatus(); }
    void setError(const char* str, Status status = Status::InternalError) { m_ctx.local.setError(str, status); }
public:
    const Router::vars_t& get_vars() const { return m_prms.vars; }
    Input& get_input() { return m_prms.input; }
    Output& get_output() { return m_output; }
    const Router::Handler3& get_h3() const { return m_prms.h3; }
    Context& get_ctx() { return m_ctx; }
    const char* getStrStatus();
    static const char* getStrStatus(Status s);
private:
    friend class StaticMongooseHandler<BaseTask>;
protected:
    Manager& m_manager;
    Router::JobParams m_prms;
    Output m_output;
    Context m_ctx;
};

class PeriodicTask : public BaseTask
{
private:
    friend class ItselfHolder<BaseTask>;
    PeriodicTask(Manager& manager, const Router::Handler3& h3, std::chrono::milliseconds timeout_ms)
        : BaseTask(manager, Router::JobParams({Input(), Router::vars_t(), h3}))
        , m_timeout_ms(timeout_ms)
    {
        start();
    }
public:
    virtual void onEvent() override;
private:
    virtual void respondAndDie(const std::string& s) override;

    void start();
private:
    std::chrono::milliseconds m_timeout_ms;
};

class ClientRequest : public BaseTask, public StaticMongooseHandler<ClientRequest>
{
private:
    friend class ItselfHolder<BaseTask>;
    ClientRequest(mg_connection *client, Router::JobParams& prms)
        : BaseTask(*Manager::from(client), prms)
        , m_client(client)
    {
    }
private:
    virtual void respondAndDie(const std::string& s) override;
private:
    friend class StaticMongooseHandler<ClientRequest>;
    void ev_handler(mg_connection *client, int ev, void *ev_data);
public:
    const mg_connection* get_client() const { return m_client; }
private:
    mg_connection *m_client;
};

class GraftServer final
{
public:
    GraftServer() : m_ready(false) { }
    void serve(mg_mgr* mgr);
private:
    static void ev_handler_empty(mg_connection *client, int ev, void *ev_data);
    static void ev_handler_http(mg_connection *client, int ev, void *ev_data);
    static void ev_handler_coap(mg_connection *client, int ev, void *ev_data);
    static int translateMethod(const char *method, std::size_t len);
    static int translateMethod(int i);

#define _M(x) std::make_pair(#x, METHOD_##x)
    constexpr static std::pair<const char *, int> m_methods[] = {
        _M(GET), _M(POST), _M(PUT), _M(DELETE), _M(HEAD) //, _M(CONNECT)
    };
public:
    bool ready() const { return m_ready; }
    void stop() { if (m_manager) m_manager->stop(); }
private:
    std::atomic_bool m_ready;
    Manager *m_manager {nullptr};
};

}//namespace graft

