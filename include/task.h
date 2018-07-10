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
using BaseTask_ptr = std::shared_ptr<BaseTask>;

class GJ_ptr;
using TPResQueue = tp::MPMCBoundedQueue< GJ_ptr >;
using GJ = GraftJob<BaseTask_ptr, TPResQueue, TaskManager>;

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
protected:
    friend class SelfHolder<BaseTask>;
    BaseTask(TaskManager& manager, const Router::JobParams& prms);
public:
    virtual ~BaseTask() { }

    virtual void finalize() = 0;
public:
    void setLastStatus(Status status) { Context::LocalFriend::setLastStatus(m_ctx.local, status); }
    Status getLastStatus() const { return m_ctx.local.getLastStatus(); }
    void setError(const char* str, Status status = Status::InternalError) { m_ctx.local.setError(str, status); }
public:
    const Router::vars_t& get_vars() const { return m_prms.vars; }
    Input& get_input() { return m_prms.input; }
    Output& get_output() { return m_output; }
    const Router::Handler3& get_h3() const { return m_prms.h3; }
    Context& get_ctx() { return m_ctx; }
public:
    TaskManager& m_manager;
    Router::JobParams m_prms;
    Output m_output;
    Context m_ctx;
};

class PeriodicTask : public BaseTask
{
private:
    friend class SelfHolder<BaseTask>;
    PeriodicTask(TaskManager& manager, const Router::Handler3& h3, std::chrono::milliseconds timeout_ms)
        : BaseTask(manager, Router::JobParams({Input(), Router::vars_t(), h3}))
        , m_timeout_ms(timeout_ms)
    {
    }
public:
    virtual void finalize() override;

    std::chrono::milliseconds m_timeout_ms;
};

class ClientTask : public BaseTask
{
private:
    friend class SelfHolder<BaseTask>;
    ClientTask(ConnectionManager* connectionManager, mg_connection *client, Router::JobParams& prms);

    virtual void finalize() override;
public:
    void ev_handler(mg_connection *client, int ev, void *ev_data);

public:
    mg_connection *m_client;
    ConnectionManager* m_connectionManager;
};

class TaskManager final
{
    void createJob(BaseTask_ptr bt);
    void onJobDoneBT(BaseTask_ptr bt, GJ* gj = nullptr); //gj equals nullptr if threadPool was skipped for some reasons
    void onCryptonDoneBT(BaseTask_ptr bt, UpstreamSender& uss);
    void onTooBusyBT(BaseTask_ptr bt); //called on the thread pool overflow

    void processResultBT(BaseTask_ptr bt);

    void respondAndDieBT(BaseTask_ptr bt, const std::string& s);
public:
    void schedule(PeriodicTask* pt);
    void onEventBT(BaseTask_ptr bt);
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

    void doWork(uint64_t cnt);

    void sendCrypton(BaseTask_ptr bt);
    void sendToThreadPool(BaseTask_ptr bt);

    void addPeriodicTask(const Router::Handler3& h3, std::chrono::milliseconds interval_ms);

    ////getters
    mg_mgr* get_mg_mgr() { return &m_mgr; }
    ThreadPoolX& get_threadPool() { return *m_threadPool.get(); }
    TPResQueue& get_resQueue() { return *m_resQueue.get(); }
    GlobalContextMap& get_gcm() { return m_gcm; }
    const ConfigOpts& get_c_opts() const { return m_copts; }
    TimerList<BaseTask_ptr>& get_timerList() { return m_timerList; }
    bool ready() const { return m_ready; }
    bool stopped() const { return m_stop; }

    ////setters
    void stop();

    ////static functions
    static void cb_event(mg_mgr* mgr, uint64_t cnt);

    static TaskManager* from(mg_mgr* mgr);

    ////events
    void onNewClient(BaseTask_ptr bt);
    void onClientDone(BaseTask_ptr bt);
public:
    void onJobDone(GJ& gj);

    void onCryptonDone(UpstreamSender& uss);

private:
    void initThreadPool(int threadCount = std::thread::hardware_concurrency(), int workersQueueSize = 32);
    void setThreadPool(ThreadPoolX&& tp, TPResQueue&& rq, uint64_t m_threadPoolInputSize_);

    bool tryProcessReadyJob();
    void processReadyJobBlock();

    void jobDone();
private:
    mg_mgr m_mgr;
    GlobalContextMap m_gcm;

    uint64_t m_cntBaseTask = 0;
    uint64_t m_cntBaseTaskDone = 0;
    uint64_t m_cntUpstreamSender = 0;
    uint64_t m_cntUpstreamSenderDone = 0;
    uint64_t m_cntJobSent = 0;
    uint64_t m_cntJobDone = 0;

    uint64_t m_threadPoolInputSize = 0;
    std::unique_ptr<ThreadPoolX> m_threadPool;
    std::unique_ptr<TPResQueue> m_resQueue;

    ConfigOpts m_copts;
    TimerList<BaseTask_ptr> m_timerList;
public:
    std::atomic_bool m_ready {false};
    std::atomic_bool m_stop {false};
};

}//namespace graft

