#pragma once

#include "mongoose.h"
#include "router.h"
#include "thread_pool.h"
#include "inout.h"
#include "context.h"
#include "timer.h"
#include "self_holder.h"

#include "CMakeConfig.h"

namespace graft {

class UpstreamSender;

class TaskManager;
class ConnectionManager;

class BaseTask;
using BaseTaskPtr = std::shared_ptr<BaseTask>;

class GJ_ptr;
using TPResQueue = tp::MPMCBoundedQueue< GJ_ptr >;
using GJ = GraftJob<BaseTaskPtr, TPResQueue, TaskManager>;

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

struct ConfigOpts
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

class BaseTask : public SelfHolder<BaseTask>
{
public:
    virtual ~BaseTask() { }
    virtual void finalize() = 0;

    void setLastStatus(Status status) { Context::LocalFriend::setLastStatus(m_ctx.local, status); }
    Status getLastStatus() const { return m_ctx.local.getLastStatus(); }
    void setError(const char* str, Status status = Status::InternalError) { m_ctx.local.setError(str, status); }

    TaskManager& getManager() { return m_manager; }
    Router::JobParams& getParams() { return m_params; }
    const Router::vars_t& getVars() const { return m_params.vars; }
    Input& getInput() { return m_params.input; }
    Output& getOutput() { return m_output; }
    const Router::Handler3& getHandler3() const { return m_params.h3; }
    Context& getCtx() { return m_ctx; }
protected:
    BaseTask(TaskManager& manager, const Router::JobParams& prms);

    TaskManager& m_manager;
    Router::JobParams m_params;
    Output m_output;
    Context m_ctx;
};

class PeriodicTask : public BaseTask
{
public:
    PeriodicTask(TaskManager& manager, const Router::Handler3& h3, std::chrono::milliseconds timeout_ms, Dummy&)
        : BaseTask(manager, Router::JobParams({Input(), Router::vars_t(), h3}))
        , m_timeout_ms(timeout_ms)
    {
    }

    virtual void finalize() override;

    std::chrono::milliseconds m_timeout_ms;
};

class ClientTask : public BaseTask
{
public:
    ClientTask(ConnectionManager* connectionManager, mg_connection *client, Router::JobParams& prms, Dummy& );

    virtual void finalize() override;

    mg_connection *m_client;
    ConnectionManager* m_connectionManager;
};

class TaskManager final
{
public:
    TaskManager(const ConfigOpts& copts)
        : m_copts(copts)
    {
        // TODO: validate options, throw exception if any mandatory options missing
        mg_mgr_init(&m_mgr, this, cb_event);
        initThreadPool(copts.workers_count, copts.worker_queue_len);
    }
    ~TaskManager();

    void serve();
    void notifyJobReady();
    void sendUpstream(BaseTaskPtr bt);
    void addPeriodicTask(const Router::Handler3& h3, std::chrono::milliseconds interval_ms);

    ////getters
    mg_mgr* getMgMgr() { return &m_mgr; }
    ThreadPoolX& getThreadPool() { return *m_threadPool.get(); }
    TPResQueue& getResQueue() { return *m_resQueue.get(); }
    GlobalContextMap& getGcm() { return m_gcm; }
    const ConfigOpts& getCopts() const { return m_copts; }
    TimerList<BaseTaskPtr>& getTimerList() { return m_timerList; }

    bool ready() const { return m_ready; }
    bool stopped() const { return m_stop; }

    ////setters
    void stop();

    ////static functions
    static void cb_event(mg_mgr* mgr, uint64_t cnt);

    static TaskManager* from(mg_mgr* mgr);

    ////events
    void onNewClient(BaseTaskPtr bt);
    void onClientDone(BaseTaskPtr bt);

    void schedule(PeriodicTask* pt);
    void onTimer(BaseTaskPtr bt);
    void onUpstreamDone(UpstreamSender& uss);
private:
    void ExecutePreAction(BaseTaskPtr bt);
    void ExecutePostAction(BaseTaskPtr bt, GJ* gj = nullptr);  //gj equals nullptr if threadPool was skipped for some reasons

    void cb_event(uint64_t cnt);
    void Execute(BaseTaskPtr bt);

    void processResultBT(BaseTaskPtr bt);

    void respondAndDieBT(BaseTaskPtr bt, const std::string& s);

    void initThreadPool(int threadCount = std::thread::hardware_concurrency(), int workersQueueSize = 32);

    bool tryProcessReadyJob();

    mg_mgr m_mgr;
    GlobalContextMap m_gcm;

    uint64_t m_cntBaseTask = 0;
    uint64_t m_cntBaseTaskDone = 0;
    uint64_t m_cntUpstreamSender = 0;
    uint64_t m_cntUpstreamSenderDone = 0;
    uint64_t m_cntJobSent = 0;
    uint64_t m_cntJobDone = 0;

    ConfigOpts m_copts;

    uint64_t m_threadPoolInputSize = 0;
    std::unique_ptr<ThreadPoolX> m_threadPool;
    std::unique_ptr<TPResQueue> m_resQueue;
    TimerList<BaseTaskPtr> m_timerList;

    std::atomic_bool m_ready {false};
    std::atomic_bool m_stop {false};
};

}//namespace graft

