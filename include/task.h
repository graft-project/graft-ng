#pragma once

#include "router.h"
#include "thread_pool.h"
#include "inout.h"
#include "context.h"
#include "timer.h"
#include "self_holder.h"
#include "log.h"
#include "serveropts.h"
#include <misc_log_ex.h>
#include "handler_api.h"

#include "supernode/server/config.h"

#include <future>
#include <deque>

#define LOG_PRINT_CLN(level,client,x) LOG_PRINT_L##level("[" << client_addr(client) << "] " << x)

#define LOG_PRINT_RQS_BT(level,bt,x) \
{ \
    ClientTask* cr = dynamic_cast<ClientTask*>(bt.get()); \
    if(cr) \
    { \
        LOG_PRINT_CLN(level,cr->m_client,x); \
    } \
    else \
    { \
        LOG_PRINT_L##level(x); \
    } \
}



struct mg_mgr;
struct mg_connection;


namespace graft
{
extern std::string client_addr(mg_connection* client);

class UpstreamSender;
class TaskManager;
class ConnectionManager;

class BaseTask;
using BaseTaskPtr = std::shared_ptr<BaseTask>;

class GJPtr;
using TPResQueue = tp::MPMCBoundedQueue< GJPtr >;
using GJ = GraftJob<BaseTaskPtr, TPResQueue, TaskManager>;
using Config = graft::supernode::server::Config;

//////////////
/// \brief The GJPtr class
/// A wrapper of GraftJob that will be moved from queue to queue with fixed size.
/// It contains single data member of unique_ptr
///
class GJPtr final
{
    std::unique_ptr<GJ> m_ptr = nullptr;
public:
    GJPtr(GJPtr&& rhs)
    {
        *this = std::move(rhs);
    }
    GJPtr& operator = (GJPtr&& rhs)
    {
        //identity check (this != &rhs) is not required, it will be done in the next copy assignment
        m_ptr = std::move(rhs.m_ptr);
        return * this;
    }

    explicit GJPtr() = default;
    GJPtr(const GJPtr&) = delete;
    GJPtr& operator = (const GJPtr&) = delete;
    ~GJPtr() = default;

    template<typename ...ARGS>
    GJPtr(ARGS&&... args) : m_ptr( new GJ( std::forward<ARGS>(args)...) )
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

using ThreadPoolX = tp::ThreadPoolImpl<tp::FixedFunction<void(), sizeof(GJPtr)>, tp::MPMCBoundedQueue>;

///////////////////////////////////

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

    const char* getStrStatus();
    static const char* getStrStatus(Status s);
protected:
    BaseTask(TaskManager& manager, const Router::JobParams& prms);

    TaskManager& m_manager;
    Router::JobParams m_params;
    Output m_output;
    Context m_ctx;
};

class UpstreamTask : public BaseTask
{
public:
    using PromiseItem = std::pair< std::promise<Input>, Output >;

    virtual void finalize() override;
    PromiseItem m_pi;
private:
    friend class SelfHolder<BaseTask>;
    UpstreamTask(TaskManager& manager, PromiseItem&& pi)
        : BaseTask(manager, Router::JobParams({Input(), Router::vars_t(),
                Router::Handler3(nullptr, nullptr, nullptr)}))
        , m_pi(std::move(pi))
    {
    }
};

class PeriodicTask : public BaseTask
{
    friend class SelfHolder<BaseTask>;
    PeriodicTask(
            TaskManager& manager, const Router::Handler3& h3,
            std::chrono::milliseconds timeout_ms,
            std::chrono::milliseconds initial_timeout_ms
    ) : BaseTask(manager, Router::JobParams({Input(), Router::vars_t(), h3}))
      , m_timeout_ms(timeout_ms), m_initial_timeout_ms(initial_timeout_ms)
    {
    }

    PeriodicTask(TaskManager& manager, const Router::Handler3& h3, std::chrono::milliseconds timeout_ms)
        : PeriodicTask(manager, h3, timeout_ms, timeout_ms)
    {
    }

    std::chrono::milliseconds m_timeout_ms;
    std::chrono::milliseconds m_initial_timeout_ms;
    bool m_initial_run {true};

public:
    virtual void finalize() override;
    std::chrono::milliseconds getTimeout();
};

class ClientTask : public BaseTask
{
    friend class SelfHolder<BaseTask>;
    ClientTask(ConnectionManager* connectionManager, mg_connection *client, Router::JobParams& prms);
public:
    virtual void finalize() override;

    mg_connection *m_client;
    ConnectionManager* m_connectionManager;
};

class StateMachine;

class TaskManager : private HandlerAPI
{
public:
    TaskManager(const ConfigOpts& copts);
    TaskManager(const Config& cfg);
    virtual ~TaskManager();

    void sendUpstream(BaseTaskPtr bt);
    void addPeriodicTask(const Router::Handler3& h3,
            std::chrono::milliseconds interval_ms, std::chrono::milliseconds initial_interval_ms = std::chrono::milliseconds::max());

    ////getters
    virtual mg_mgr* getMgMgr()  = 0;
    GlobalContextMap& getGcm() { return m_gcm; }
    ConfigOpts& getCopts() { return m_copts; }

    GlobalContextMap& gcm(void) { return m_gcm; }
    Config& config(void) { return m_cfg; }

    TimerList<BaseTaskPtr>& getTimerList() { return m_timerList; }

    static TaskManager* from(mg_mgr* mgr);

    ////events
    void onNewClient(BaseTaskPtr bt);
    void onClientDone(BaseTaskPtr bt);

    void schedule(PeriodicTask* pt);
    void onTimer(BaseTaskPtr bt);
    void onUpstreamDone(UpstreamSender& uss);

    virtual void sendUpstreamBlocking(Output& output, Input& input, std::string& err) override;
    virtual bool addPeriodicTask(const Router::Handler& h_worker,
                                 std::chrono::milliseconds interval_ms,
                                 std::chrono::milliseconds initial_interval_ms = std::chrono::milliseconds::max()) override;

    void runWorkerActionFromTheThreadPool(BaseTaskPtr bt);

    virtual void notifyJobReady() = 0;

    void cb_event(uint64_t cnt);
protected:
    bool canStop();
    void executePostponedTasks();
    void setIOThread(bool current);
    void checkUpstreamBlockingIO();
    void checkPeriodicTaskIO();

    ConfigOpts m_copts;
    Config m_cfg;

private:
    void Execute(BaseTaskPtr bt);
    void processForward(BaseTaskPtr bt);
    void processOk(BaseTaskPtr bt);
    void respondAndDie(BaseTaskPtr bt, const std::string& s, bool die = true);
    void postponeTask(BaseTaskPtr bt);

    void checkThreadPoolOverflow(BaseTaskPtr bt);
    void runPreAction(BaseTaskPtr bt);
    void runWorkerAction(BaseTaskPtr bt);
    void runPostAction(BaseTaskPtr bt);

    void initThreadPool(int threadCount = std::thread::hardware_concurrency(), int workersQueueSize = 32);
    bool tryProcessReadyJob();

    static inline size_t next_pow2(size_t val);

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
    TimerList<BaseTaskPtr> m_timerList;

    std::map<Context::uuid_t, BaseTaskPtr> m_postponedTasks;
    std::deque<BaseTaskPtr> m_readyToResume;
    std::priority_queue<std::pair<std::chrono::time_point<std::chrono::steady_clock>,Context::uuid_t>> m_expireTaskQueue;

    using PromiseItem = UpstreamTask::PromiseItem;
    using PromiseQueue = tp::MPMCBoundedQueue<PromiseItem>;

    using PeridicTaskItem = std::tuple<Router::Handler3, std::chrono::milliseconds, std::chrono::milliseconds>;
    using PeriodicTaskQueue = tp::MPMCBoundedQueue<PeridicTaskItem>;

    std::unique_ptr<PromiseQueue> m_promiseQueue;
    std::unique_ptr<PeriodicTaskQueue> m_periodicTaskQueue;
    static thread_local bool io_thread;

    friend class StateMachine;
    std::unique_ptr<StateMachine> m_stateMachine;
};

}//namespace graft

