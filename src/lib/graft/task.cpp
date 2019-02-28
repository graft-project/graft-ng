
#include "lib/graft/task.h"
#include "lib/graft/connection.h"
#include "lib/graft/router.h"
#include "lib/graft/state_machine.h"
#include "lib/graft/handler_api.h"
#include "lib/graft/expiring_list.h"
#include "lib/graft/sys_info.h"
#include "lib/graft/common/utils.h"

#undef MONERO_DEFAULT_LOG_CATEGORY
#define MONERO_DEFAULT_LOG_CATEGORY "supernode.task"

namespace graft {

thread_local bool TaskManager::io_thread = false;

void StateMachine::process(BaseTaskPtr bt)
{
    static const char *state_strs[] = { GRAFT_STATE_LIST(EXP_TO_STR) };

    St cur_stat = status(bt);

    for(auto& r : m_table)
    {
        if(m_state != std::get<smStateStart>(r)) continue;

        Statuses& ss = std::get<smStatuses>(r);
        if(ss.size()!=0)
        {
            bool res = false;
            for(auto s : ss)
            {
                if(s == cur_stat)
                {
                    res = true;
                    break;
                }
            }
            if(!res) continue;
        }

        Guard& g = std::get<smGuard>(r);
        if(g && !g(bt)) continue;

        Action& a = std::get<smAction>(r);
        if(a) a(bt);

        State prev_state = m_state;
        m_state = std::get<smStateEnd>(r);

        mlog_current_log_category = "sm";
        LOG_PRINT_RQS_BT(3,bt, "sm: " << state_strs[int(prev_state)] << "->" << state_strs[int(m_state)] );
        mlog_current_log_category.clear();

        return;
    }
    {
        bool is_periodic = (dynamic_cast<PeriodicTask*>(bt.get()) != nullptr);
        std::ostringstream oss;
        oss << (is_periodic? "periodic;" : "") << " state " << state_strs[int(m_state)] << " status " << bt->getStrStatus();
        const Router::Handler3& h3 = bt->getHandler3();
        oss << "{" << !!h3.pre_action << "," << !!h3.worker_action << "," << !!h3.post_action << "}";
        throw std::runtime_error("State machine table is not complete." + oss.str());
    }
}

const StateMachine::Guard StateMachine::has(Router::Handler H3::* act)
{
    return [act](BaseTaskPtr bt)->bool
    {
        return (bt->getHandler3().*act != nullptr);
    };
}

const StateMachine::Guard StateMachine::hasnt(Router::Handler H3::* act)
{
    return [act](BaseTaskPtr bt)->bool
    {
        return (bt->getHandler3().*act == nullptr);
    };
}

void StateMachine::init_table()
{

    const Action run_forward = [](BaseTaskPtr bt)
    {
        bt->getManager().processForward(bt);
    };

    const Action run_response = [](BaseTaskPtr bt)
    {
        assert(St::Again == bt->getLastStatus());
        bt->getManager().respondAndDie(bt, bt->getOutput().data(), false);
    };

    const Action run_error_response = [](BaseTaskPtr bt)
    {
        assert(St::Error == bt->getLastStatus() ||
               St::InternalError == bt->getLastStatus() ||
               St::Stop == bt->getLastStatus());
        bt->getManager().respondAndDie(bt, bt->getOutput().data());
    };

    const Action run_drop = [](BaseTaskPtr bt)
    {
        assert(St::Drop == bt->getLastStatus());
        bt->getManager().respondAndDie(bt, "Job done Drop."); //TODO: Expect HTTP Error Response
    };

    const Action run_ok_response = [](BaseTaskPtr bt)
    {
        assert(St::Ok == bt->getLastStatus());
        bt->getManager().processOk(bt);
    };

    const Action run_postpone = [](BaseTaskPtr bt)
    {
        assert(St::Postpone == bt->getLastStatus());
        bt->getManager().postponeTask(bt);
    };

    const Action check_overflow = [](BaseTaskPtr bt)
    {
        bt->getManager().checkThreadPoolOverflow(bt);
    };

    const Action run_preaction = [](BaseTaskPtr bt)
    {
        bt->getManager().runPreAction(bt);
    };

    const Action run_workeraction = [](BaseTaskPtr bt)
    {
        bt->getManager().runWorkerAction(bt);
    };

    const Action run_postaction = [](BaseTaskPtr bt)
    {
        bt->getManager().runPostAction(bt);
    };

#define ANY { }

    m_table = std::vector<row>(
    {
//      Start                   Status          Target              Guard               Action
        {EXECUTE,               ANY,            PRE_ACTION,         nullptr,            check_overflow },
        {PRE_ACTION,            {St::Busy},     EXIT,               nullptr,            nullptr },
        {PRE_ACTION,            {St::None, St::Ok, St::Forward, St::Postpone},
                                                CHK_PRE_ACTION,     nullptr,            run_preaction },
        {CHK_PRE_ACTION,        {St::Again},    PRE_ACTION,         nullptr,            run_response },
        {CHK_PRE_ACTION,        {St::Ok},       WORKER_ACTION,      has(&H3::pre_action), nullptr },
        {CHK_PRE_ACTION,        {St::Forward},  POST_ACTION,        has(&H3::pre_action), nullptr },
        {CHK_PRE_ACTION,        {St::Error, St::InternalError, St::Stop},
                                                EXIT,               has(&H3::pre_action), run_error_response },
        {CHK_PRE_ACTION,        {St::Drop},     EXIT,               has(&H3::pre_action), run_drop },
        {CHK_PRE_ACTION,        {St::None, St::Ok, St::Forward, St::Postpone},
                                                WORKER_ACTION,      nullptr,            nullptr },
        {WORKER_ACTION,         ANY,            CHK_WORKER_ACTION,  nullptr,            run_workeraction },
        {CHK_WORKER_ACTION,     ANY,            EXIT,               has(&H3::worker_action), nullptr },
        {CHK_WORKER_ACTION,     ANY,            POST_ACTION,        nullptr,            nullptr },

        {WORKER_ACTION_DONE,    {St::Again},    WORKER_ACTION,      nullptr,            run_response },
        {WORKER_ACTION_DONE,    ANY,            POST_ACTION,        nullptr,            nullptr },
        {POST_ACTION,           ANY,            CHK_POST_ACTION,    nullptr,            run_postaction },
        {CHK_POST_ACTION,       {St::Again},    POST_ACTION,        nullptr,            run_response },
        {CHK_POST_ACTION,       {St::Forward},  EXIT,               nullptr,            run_forward },
        {CHK_POST_ACTION,       {St::Ok},       EXIT,               nullptr,            run_ok_response },
        {CHK_POST_ACTION,       {St::Error, St::InternalError, St::Stop},
                                                EXIT,               nullptr,            run_error_response },
        {CHK_POST_ACTION,       {St::Drop},     EXIT,               nullptr,            run_drop },
        {CHK_POST_ACTION,       {St::Postpone}, EXIT,               nullptr,            run_postpone },
    });

#undef ANY

}

class Uuid_Input : private std::pair<Context::uuid_t,std::shared_ptr<Input>>
{
public:
    Uuid_Input() = default;
    Uuid_Input(const Context::uuid_t& uuid) { first = uuid; }
    Uuid_Input(const Context::uuid_t& uuid, const Input& input) { first = uuid; second = std::make_shared<Input>(input); }
    Uuid_Input(const Context::uuid_t& uuid, Input&& input) { first = uuid; second = std::make_shared<Input>(input); }
    Uuid_Input(Context::uuid_t&& uuid) { first = std::move(uuid); }
    Uuid_Input(const Uuid_Input& ui) { *this = ui; }
    Uuid_Input& operator = (const Uuid_Input& ui) { first = ui.first; second = ui.second; return *this; }
    Uuid_Input(Uuid_Input&& ui) = default;
    Uuid_Input& operator = (Uuid_Input&& ui) = default;
    ~Uuid_Input() = default;
    bool operator == (const Uuid_Input& ui) const { return first == ui.first; }

    std::shared_ptr<Input>& getInputPtr() { return second; }
};

class ExpiringList : public detail::ExpiringListT< Uuid_Input >
{
public:
    ExpiringList(int life_time_ms)
        : detail::ExpiringListT< Uuid_Input >( life_time_ms )
    { }
};

class UpstreamManager
{
public:
    using OnDoneCallback = std::function<void(UpstreamSender& uss)>;

    UpstreamManager(TaskManager& manager, OnDoneCallback onDoneCallback)
        : m_manager(manager)
        , m_onDoneCallback(onDoneCallback)
    { init(); }

    bool busy() const
    {
        return (m_cntUpstreamSender != m_cntUpstreamSenderDone);
    }

    void send(BaseTaskPtr bt)
    {
        ConnItem* connItem = &m_default;
        {//find connItem
            const std::string& uri = bt->getOutput().uri;
            if(!uri.empty() && uri[0] == '$')
            {//substitutions
                auto it = m_conn2item.find(uri.substr(1));
                if(it == m_conn2item.end())
                {
                    std::ostringstream oss;
                    oss << "cannot find uri substitution '" << uri << "'";
                    throw std::runtime_error(oss.str());
                }
                connItem = &it->second;
            }
        }
        if(connItem->m_maxConnections != 0 && connItem->m_idleConnections.empty() && connItem->m_connCnt == connItem->m_maxConnections)
        {
            connItem->m_taskQueue.push_back(bt);
            return;
        }

        createUpstreamSender(connItem, bt);
    }
private:
    uint64_t m_cntUpstreamSender = 0;
    uint64_t m_cntUpstreamSenderDone = 0;

    class ConnItem
    {
    public:
        using Active = bool;
        using ConnectionId = uint64_t;

        ConnItem() = default;
        ConnItem(int uriId, const std::string& uri, int maxConnections, bool keepAlive, double timeout)
            : m_uriId(uriId)
            , m_uri(uri)
            , m_maxConnections(maxConnections)
            , m_keepAlive(keepAlive)
            , m_timeout(timeout)
        {
        }
        std::pair<ConnectionId, mg_connection*> getConnection()
        {
            //TODO: something wrong with (m_connCnt <= m_maxConnections)
            assert(m_maxConnections == 0 || m_connCnt <= m_maxConnections);
            std::pair<ConnectionId, mg_connection*> res = std::make_pair(0,nullptr);
            if(!m_keepAlive)
            {
                ++m_connCnt;
                return res;
            }
            if(!m_idleConnections.empty())
            {
                auto it = m_idleConnections.begin();
                res = std::make_pair(it->second, it->first);
                m_idleConnections.erase(it);
            }
            else
            {
                ++m_connCnt;
                res.first = ++m_newId;
            }
            auto res1 = m_activeConnections.emplace(res);
            assert(res1.second);
            assert(m_connCnt == m_idleConnections.size() + m_activeConnections.size());
            return res;
        }

        void releaseActive(ConnectionId connectionId, mg_connection* client)
        {
            assert(m_keepAlive || ((connectionId == 0) && (client == nullptr)));
            if(!m_keepAlive) return;
            auto it = m_activeConnections.find(connectionId);
            assert(it != m_activeConnections.end());
            assert(it->second == nullptr || client == nullptr || it->second == client);
            if(client != nullptr)
            {
                m_idleConnections.emplace(client, it->first);
                m_upstreamStub.setConnection(client);
            }
            else
            {
                --m_connCnt;
            }
            m_activeConnections.erase(it);
        }

        void onCloseIdle(mg_connection* client)
        {
            assert(m_keepAlive);
            auto it = m_idleConnections.find(client);
            assert(it != m_idleConnections.end());
            --m_connCnt;
            m_idleConnections.erase(it);
        }

        ConnectionId m_newId = 0;
        int m_connCnt = 0;
        int m_uriId;
        std::string m_uri;
        double m_timeout;
        //assert(m_upstreamQueue.empty() || 0 < m_maxConn);
        int m_maxConnections;
        std::deque<BaseTaskPtr> m_taskQueue;
        bool m_keepAlive = false;
        std::map<mg_connection*, ConnectionId> m_idleConnections;
        std::map<ConnectionId, mg_connection*> m_activeConnections;
        UpstreamStub m_upstreamStub;
    };

    void onDone(UpstreamSender& uss, ConnItem* connItem, ConnItem::ConnectionId connectionId, mg_connection* client)
    {
        ++m_cntUpstreamSenderDone;
        m_onDoneCallback(uss);
        connItem->releaseActive(connectionId, client);
        if(connItem->m_taskQueue.empty()) return;
        BaseTaskPtr bt = connItem->m_taskQueue.front(); connItem->m_taskQueue.pop_front();
        createUpstreamSender(connItem, bt);
    }

    void init()
    {
        int uriId = 0;
        const ConfigOpts& opts = m_manager.getCopts();
        m_default = ConnItem(uriId++, opts.cryptonode_rpc_address.c_str(), 0, false, opts.upstream_request_timeout);

        for(auto& subs : OutHttp::uri_substitutions)
        {
            double timeout = std::get<3>(subs.second);
            if(timeout < 1e-5) timeout = opts.upstream_request_timeout;
            auto res = m_conn2item.emplace(subs.first, ConnItem(uriId, std::get<0>(subs.second), std::get<1>(subs.second), std::get<2>(subs.second), timeout));
            assert(res.second);
            ConnItem* connItem = &res.first->second;
            connItem->m_upstreamStub.setCallback([connItem](mg_connection* client){ connItem->onCloseIdle(client); });
        }
    }

    void createUpstreamSender(ConnItem* connItem, BaseTaskPtr bt)
    {
        auto onDoneAct = [this, connItem](UpstreamSender& uss, uint64_t connectionId, mg_connection* client)
        {
            onDone(uss, connItem, connectionId, client);
        };

        ++m_cntUpstreamSender;
        UpstreamSender::Ptr uss;
        if(connItem->m_keepAlive)
        {
            auto res = connItem->getConnection();
            uss = UpstreamSender::Create(bt, onDoneAct, res.first, res.second, connItem->m_timeout);
        }
        else
        {
            uss = UpstreamSender::Create(bt, onDoneAct, connItem->m_timeout);
        }

        const std::string& uri = (connItem != &m_default || bt->getOutput().uri.empty())? connItem->m_uri : bt->getOutput().uri;
        uss->send(m_manager, uri);
    }

    using Uri2ConnItem = std::map<std::string, ConnItem>;

    OnDoneCallback m_onDoneCallback;

    ConnItem m_default;
    Uri2ConnItem m_conn2item;
    TaskManager& m_manager; //TODO: should be removed, and be independent of TaskManager
};

TaskManager::TaskManager(const ConfigOpts& copts, SysInfoCounter& sysInfoCounter)
    : m_copts(copts)
    , m_sysInfoCounter(sysInfoCounter)
    , m_gcm(this)
    , m_futurePostponeUuids(std::make_unique<ExpiringList>(1000 * copts.http_connection_timeout))
    , m_stateMachine(std::make_unique<StateMachine>())
{
    copts.check_asserts();

    // TODO: validate options, throw exception if any mandatory options missing
    initThreadPool(copts.workers_count, copts.worker_queue_len, copts.workers_expelling_interval_ms);
}

TaskManager::~TaskManager()
{

}

inline size_t TaskManager::next_pow2(size_t val)
{
    --val;
    for(size_t i = 1; i<sizeof(val)*8; i<<=1)
    {
        val |= val >> i;
    }
    return ++val;
}

bool TaskManager::addPeriodicTask(const Router::Handler& h_worker,
                                  std::chrono::milliseconds interval_ms,
                                  std::chrono::milliseconds initial_interval_ms,
                                  double random_factor)
{
    if(io_thread)
    {//it is called from pre_action or post_action, and we can call requestAddPeriodicTask directly
        addPeriodicTask({nullptr, h_worker, nullptr}, interval_ms, initial_interval_ms, random_factor);
        return true;
    }
    else
    {
        PeridicTaskItem item = std::make_tuple(Router::Handler3(nullptr, h_worker, nullptr), interval_ms, initial_interval_ms, random_factor);
        bool ok = m_periodicTaskQueue->push( std::move(item) );
        if(!ok) return false;
        notifyJobReady();
        return true;
    }
}

request::system_info::Counter& TaskManager::runtimeSysInfo()
{
    return m_sysInfoCounter;
}

const ConfigOpts& TaskManager::configOpts() const
{
    return m_copts;
}

void TaskManager::checkPeriodicTaskIO()
{
    while(true)
    {
        PeridicTaskItem pti;
        bool res = m_periodicTaskQueue->pop(pti);
        if(!res) break;
        Router::Handler3& h3 = std::get<0>(pti);
        std::chrono::milliseconds& interval_ms = std::get<1>(pti);
        std::chrono::milliseconds& initial_interval_ms = std::get<2>(pti);
        double& random_factor = std::get<3>(pti);
        addPeriodicTask(h3, interval_ms, initial_interval_ms, random_factor);
    }
}

//pay attension, input is output and vice versa
void TaskManager::sendUpstreamBlocking(Output& output, Input& input, std::string& err)
{
    if(io_thread) throw std::logic_error("the function sendUpstreamBlocking should not be called in IO thread");
    std::promise<Input> promise;
    std::future<Input> future = promise.get_future();
    std::pair< std::promise<Input>, Output> pair = std::make_pair( std::move(promise), output);
    m_promiseQueue->push( std::move(pair) );
    notifyJobReady();
    err.clear();
    try
    {
        input = future.get();
    }
    catch(std::exception& ex)
    {
        err = ex.what();
    }
}

void TaskManager::checkUpstreamBlockingIO()
{
    while(true)
    {
        PromiseItem pi;
        bool res = m_promiseQueue->pop(pi);
        if(!res) break;
        UpstreamTask::Ptr bt = BaseTask::Create<UpstreamTask>(*this, std::move(pi));
        assert(m_upstreamManager);
        m_upstreamManager->send(bt);
    }
}

void TaskManager::sendUpstream(BaseTaskPtr bt)
{
    assert(m_upstreamManager);
    m_upstreamManager->send(bt);
}

void TaskManager::onTimer(BaseTaskPtr bt)
{
    assert(dynamic_cast<PeriodicTask*>(bt.get()));
    bt->setLastStatus(Status::None);
    Execute(bt);
}

void TaskManager::respondAndDie(BaseTaskPtr bt, const std::string& s, bool die)
{
    ClientTask* ct = dynamic_cast<ClientTask*>(bt.get());
    if(ct)
    {
        ct->m_connectionManager->respond(ct, s);
    }
    else
    {
        assert( dynamic_cast<PeriodicTask*>(bt.get()) );
    }

    Context::uuid_t uuid = bt->getCtx().getId(false);
    if(!uuid.is_nil())
    {
        auto it = m_postponedTasks.find(uuid);
        if (it != m_postponedTasks.end())
            m_postponedTasks.erase(it);
    }

    if(die)
        bt->finalize();
}

void TaskManager::schedule(PeriodicTask* pt)
{
    m_timerList.push(pt->getTimeout(), pt->getSelf());
}

bool TaskManager::canStop()
{
    return (m_cntBaseTask == m_cntBaseTaskDone)
            && (!m_upstreamManager->busy())
            && (m_cntJobSent == m_cntJobDone);
}

bool TaskManager::tryProcessReadyJob()
{
    GJPtr gj;
    bool res = m_resQueue->pop(gj);
    if(!res) return res;
    ++m_cntJobDone;
    BaseTaskPtr bt = gj->getTask();

    LOG_PRINT_RQS_BT(2,bt,"worker_action completed with result " << bt->getStrStatus());
    m_stateMachine->dispatch(bt, StateMachine::State::WORKER_ACTION_DONE);
    return true;
}

void TaskManager::Execute(BaseTaskPtr bt)
{
    m_stateMachine->dispatch(bt, StateMachine::State::EXECUTE);
}

void TaskManager::checkThreadPoolOverflow(BaseTaskPtr bt)
{
    auto& params = bt->getParams();

    assert(m_cntJobDone <= m_cntJobSent);
    if(params.h3.worker_action && m_cntJobSent - m_cntJobDone == m_threadPoolInputSize)
    {//check overflow
        bt->getCtx().local.setError("Service Unavailable", Status::Busy);
        respondAndDie(bt,"Thread pool overflow");
    }
    assert(m_cntJobSent - m_cntJobDone <= m_threadPoolInputSize);
}

void TaskManager::runPreAction(BaseTaskPtr bt)
{
    auto& params = bt->getParams();

    if(!params.h3.pre_action) return;

    auto& ctx = bt->getCtx();
    auto& output = bt->getOutput();

    try
    {
        // Please read the comment about exceptions and noexcept specifier
        // near 'void terminate()' function in main.cpp
        mlog_current_log_category = params.h3.name;
        Status status = params.h3.pre_action(params.vars, params.input, ctx, output);
        mlog_current_log_category.clear();

        bt->setLastStatus(status);
        if(Status::Ok == status && (params.h3.worker_action || params.h3.post_action)
                || Status::Forward == status)
        {
            params.input.assign(output);
        }
    }
    catch(const std::exception& e)
    {
        bt->setError(e.what());
        params.input.reset();
        throw;
    }
    catch(...)
    {
        bt->setError("unknown exception");
        params.input.reset();
        throw;
    }
    LOG_PRINT_RQS_BT(3,bt,"pre_action completed with result " << bt->getStrStatus());
}

void TaskManager::runWorkerAction(BaseTaskPtr bt)
{
    auto& params = bt->getParams();

    if(params.h3.worker_action)
    {
        ++m_cntJobSent;
        m_threadPool->post(
                    GJPtr( bt, m_resQueue.get(), this ),
                    true
                    );
    }
}

//the function is called from the Thread Pool
//So pay attension this is another thread than others member functions
void TaskManager::runWorkerActionFromTheThreadPool(BaseTaskPtr bt)
{
    auto& params = bt->getParams();
    auto& ctx = bt->getCtx();
    auto& output = bt->getOutput();

    try
    {
        // Please read the comment about exceptions and noexcept specifier
        // near 'void terminate()' function in main.cpp

        mlog_current_log_category = params.h3.name;
        Status status = params.h3.worker_action(params.vars, params.input, ctx, output);
        mlog_current_log_category.clear();

        bt->setLastStatus(status);
        if(Status::Ok == status && params.h3.post_action || Status::Forward == status)
        {
            params.input.assign(output);
        }
    }
    catch(const std::exception& e)
    {
        ctx.local.setError(e.what());
        params.input.reset();
        throw;
    }
    catch(...)
    {
        ctx.local.setError("unknown exception");
        params.input.reset();
        throw;
    }

}

void TaskManager::runPostAction(BaseTaskPtr bt)
{
    auto& params = bt->getParams();

    if(!params.h3.post_action) return;

    auto& ctx = bt->getCtx();
    auto& output = bt->getOutput();

    try
    {
        mlog_current_log_category = params.h3.name;
        Status status = params.h3.post_action(params.vars, params.input, ctx, output);
        mlog_current_log_category.clear();

        //in case of pre_action or worker_action return Forward we call post_action in any case
        //but we should ignore post_action result status and output
        if(Status::Forward != bt->getLastStatus())
        {
            bt->setLastStatus(status);
            if(Status::Forward == status)
            {
                params.input.assign(output);
            }
        }
    }
    catch(const std::exception& e)
    {
        bt->setError(e.what());
        params.input.reset();
        throw;
    }
    catch(...)
    {
        bt->setError("unknown exception");
        params.input.reset();
        throw;
    }
    LOG_PRINT_RQS_BT(3,bt,"post_action completed with result " << bt->getStrStatus());
}

void TaskManager::postponeTask(BaseTaskPtr bt)
{
    Context::uuid_t uuid = bt->getCtx().getId();
    assert(!uuid.is_nil());

    //find already recieved uuid
    auto res = m_futurePostponeUuids->extract(uuid);
    if(res.first)
    {//found
        //set saved input
        assert(res.second.getInputPtr());
        bt->getParams().input = *res.second.getInputPtr();
        m_readyToResume.push_back(bt);
        LOG_PRINT_RQS_BT(2,bt,"for the task with uuid '" << uuid << "' an answer found; it will be resumed.");
        return;
    }

    assert(m_postponedTasks.find(uuid) == m_postponedTasks.end());
    m_postponedTasks[uuid] = bt;
    std::chrono::duration<double> timeout(m_copts.http_connection_timeout);
    std::chrono::steady_clock::time_point tpoint = std::chrono::steady_clock::now()
            + std::chrono::duration_cast<std::chrono::steady_clock::duration>( timeout );
    m_expireTaskQueue.push(std::make_pair(
                                    tpoint,
                                    uuid)
                                );
    LOG_PRINT_RQS_BT(2,bt,"task with uuid '" << uuid << "' postponed.");
}

void TaskManager::executePostponedTasks()
{
    while(!m_readyToResume.empty())
    {
        BaseTaskPtr& bt = m_readyToResume.front();
        Context::uuid_t uuid = bt->getCtx().getId();
        LOG_PRINT_RQS_BT(2,bt,"task with uuid '" << uuid << "' resumed.");
        Execute(bt);
        m_readyToResume.pop_front();
    }

    if(m_expireTaskQueue.empty()) return;

    auto now = std::chrono::steady_clock::now();
    while(!m_expireTaskQueue.empty())
    {
        auto& pair = m_expireTaskQueue.top();
        if(now <= pair.first) break;

        auto it = m_postponedTasks.find(pair.second);
        if(it != m_postponedTasks.end())
        {
            BaseTaskPtr& bt = it->second;
            Context::uuid_t uuid = bt->getCtx().getId();
            LOG_PRINT_RQS_BT(2,bt,"postponed task with uuid '" << uuid << "' expired.");
            std::string msg = "Postpone task response timeout";
            bt->setError(msg.c_str(), Status::Error);
            respondAndDie(bt, msg);
        }

        m_expireTaskQueue.pop();
    }
}

void TaskManager::expelWorkers()
{
    if(getCopts().workers_expelling_interval_ms == 0) return;
    m_threadPool->expelWorkers();
}

void TaskManager::getThreadPoolInfo(uint64_t& activeWorkers, uint64_t& expelledWorkers) const
{
    activeWorkers = m_threadPool->getActiveWorkersCount();
    expelledWorkers = m_threadPool->getExpelledWorkersCount();
}


void TaskManager::processForward(BaseTaskPtr bt)
{
    assert(Status::Forward == bt->getLastStatus());
    LOG_PRINT_RQS_BT(3,bt,"Sending request to CryptoNode");
    sendUpstream(bt);
}

void TaskManager::processOk(BaseTaskPtr bt)
{
    Context::uuid_t nextUuid = bt->getCtx().getNextTaskId();
    if(!nextUuid.is_nil())
    {
        auto it = m_postponedTasks.find(nextUuid);
        if(it == m_postponedTasks.end())
        {
            LOG_PRINT_RQS_BT(2,bt,"attempt to resume task with uuid '" << nextUuid << "' failed, maybe it is not postponed yet.");
            Input input = bt->getInput();
            m_futurePostponeUuids->add(Uuid_Input(nextUuid, std::move(input)));
        }
        else
        {
            LOG_PRINT_RQS_BT(2,bt,"resuming task with uuid '" << nextUuid << "'.");
            //redirect callback input to postponed task
            BaseTaskPtr& bt_next = it->second;
            bt_next->getInput() = bt->getInput();

            m_readyToResume.push_back(bt_next);
            m_postponedTasks.erase(it);
        }
    }
    respondAndDie(bt, bt->getOutput().data());
}

void TaskManager::addPeriodicTask(
        const Router::Handler3& h3, std::chrono::milliseconds interval_ms, std::chrono::milliseconds initial_interval_ms, double random_factor)
{
    if(initial_interval_ms == std::chrono::milliseconds::max()) initial_interval_ms = interval_ms;

    BaseTask* bt = BaseTask::Create<PeriodicTask>(*this, h3, interval_ms, initial_interval_ms, random_factor).get();
    PeriodicTask* pt = dynamic_cast<PeriodicTask*>(bt);
    assert(pt);
    schedule(pt);
}

void TaskManager::onNewClient(BaseTaskPtr bt)
{
    ++m_cntBaseTask;
    Execute(bt);
}

void TaskManager::onClientDone(BaseTaskPtr bt)
{
    ++m_cntBaseTaskDone;
}

void TaskManager::initThreadPool(int threadCount, int workersQueueSize, int expellingIntervalMs)
{
    if(threadCount <= 0) threadCount = std::thread::hardware_concurrency();
    threadCount = next_pow2(threadCount);
    if(workersQueueSize <= 0) workersQueueSize = 32;

    tp::ThreadPoolOptions th_op;
    th_op.setThreadCount(threadCount);
    th_op.setQueueSize(workersQueueSize);
    th_op.setExpellingIntervalMs(expellingIntervalMs);
    graft::ThreadPoolX thread_pool(th_op);

    const size_t maxinputSize = th_op.threadCount()*th_op.queueSize();
    size_t resQueueSize = next_pow2( maxinputSize );
    graft::TPResQueue resQueue(resQueueSize);

    m_threadPool = std::make_unique<ThreadPoolX>(std::move(thread_pool));
    m_resQueue = std::make_unique<TPResQueue>(std::move(resQueue));
    m_threadPoolInputSize = maxinputSize;
    m_promiseQueue = std::make_unique<PromiseQueue>( threadCount );
    //TODO: it is not clear how many items we need in PeriodicTaskQueue, maybe we should make it dynamically but this requires additional synchronization
    m_periodicTaskQueue = std::make_unique<PeriodicTaskQueue>(2*threadCount);
    m_upstreamManager = std::make_unique<UpstreamManager>(*this, [this](UpstreamSender& uss){ onUpstreamDone(uss); } );

    LOG_PRINT_L1("Thread pool created with " << threadCount
                 << " workers with " << workersQueueSize
                 << " queue size each. The output queue size is " << resQueueSize);
}

void TaskManager::setIOThread(bool current)
{
    io_thread = current;
}

void TaskManager::cb_event(uint64_t cnt)
{
    //When multiple threads write to the output queue of the thread pool.
    //It is possible that a hole appears when a thread has not completed to set
    //the cell data in the queue. The hole leads to failure of pop operations.
    //Thus, it is better to process as many cells as we can without waiting when
    //the cell will be filled, instead of basing on the counter.
    //We cannot lose any cell because a notification follows the hole completion.

    while(true)
    {
        bool res = tryProcessReadyJob();
        if(!res) break;
    }
}

void TaskManager::onUpstreamDone(UpstreamSender& uss)
{
    upstreamDoneProcess(uss);
    //uss will be destroyed on exit
}

void TaskManager::upstreamDoneProcess(UpstreamSender& uss)
{
    if(Status::Ok == uss.getStatus())
        runtimeSysInfo().count_upstrm_http_resp_ok();
    else
        runtimeSysInfo().count_upstrm_http_resp_err();

    BaseTaskPtr bt = uss.getTask();
    UpstreamTask* ust = dynamic_cast<UpstreamTask*>(bt.get());
    if(ust)
    {
        try
        {
            if(Status::Ok != uss.getStatus())
            {
                throw std::runtime_error(uss.getError().c_str());
            }
            ust->m_pi.first.set_value(bt->getInput());
        }
        catch(std::exception&)
        {
            ust->m_pi.first.set_exception(std::current_exception());
        }
        return;
    }
    if(Status::Ok != uss.getStatus())
    {
        bt->setError(uss.getError().c_str(), uss.getStatus());
        LOG_PRINT_RQS_BT(2,bt, "CryptoNode done with error: " << uss.getError().c_str());
        assert(Status::Error == bt->getLastStatus()); //Status::Error only possible value now
        respondAndDie(bt, bt->getOutput().data());

        return;
    }
    //here you can send a job to the thread pool or send response to client
    //uss will be destroyed on exit, save its result
    {//now always create a job and put it to the thread pool after CryptoNode
        LOG_PRINT_RQS_BT(2,bt, "CryptoNode answered : '" << make_dump_output( bt->getInput().body, getCopts().log_trunc_to_size ) << "'");
        if(!bt->getSelf())
        {//it is possible that a client has closed connection already
            return;
        }
        Execute(bt);
    }
}

BaseTask::BaseTask(TaskManager& manager, const Router::JobParams& params)
    : m_manager(manager)
    , m_params(params)
    , m_ctx(manager.getGcm())
{
}

const char* BaseTask::getStrStatus(Status s)
{
    assert(s<=Status::Stop);
    static const char *status_str[] = { GRAFT_STATUS_LIST(EXP_TO_STR) };
    return status_str[static_cast<int>(s)];
}

const char* BaseTask::getStrStatus()
{
    return getStrStatus(m_ctx.local.getLastStatus());
}

void UpstreamTask::finalize()
{
    releaseItself();
}

void PeriodicTask::finalize()
{
    if(m_ctx.local.getLastStatus() == Status::Stop)
    {
        LOG_PRINT_L2("Timer request stopped with result " << getStrStatus());
        releaseItself();
        return;
    }
    m_manager.schedule(this);
}

std::chrono::milliseconds PeriodicTask::getTimeout()
{
    if(m_initial_run)
    {
        m_initial_run = false;
        return m_initial_timeout_ms;
    }
    if(m_random_factor < 0.0001) return m_timeout_ms;
    using i_type = decltype(m_timeout_ms.count());
    i_type v = graft::utils::random_number(m_timeout_ms.count(), (i_type)(m_timeout_ms.count()*(1.0 + m_random_factor)));
    return std::chrono::milliseconds(v);
}

ClientTask::ClientTask(ConnectionManager* connectionManager, mg_connection *client, Router::JobParams& prms)
    : BaseTask(ConnectionBase::from( getMgr(client) )->getLooper(), prms)
    , m_connectionManager(connectionManager)
    , m_client(client)
{
}

void ClientTask::finalize()
{
    releaseItself();
}

}//namespace graft
