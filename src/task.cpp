#include "task.h"
#include "connection.h"
#include "router.h"
#include "state_machine.h"

#undef MONERO_DEFAULT_LOG_CATEGORY
#define MONERO_DEFAULT_LOG_CATEGORY "supernode.task"

namespace graft {

thread_local bool TaskManager::io_thread = false;
TaskManager* TaskManager::g_upstreamManager{nullptr};

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
    throw std::runtime_error("State machine table is not complete");
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

TaskManager::TaskManager(const ConfigOpts& copts) : m_copts(copts)
{
    // TODO: validate options, throw exception if any mandatory options missing
    initThreadPool(copts.workers_count, copts.worker_queue_len);
    m_stateMachine = std::make_unique<StateMachine>();
}

TaskManager::~TaskManager()
{

}

//pay attension, input is output and vice versa
void TaskManager::sendUpstreamBlocking(Output& output, Input& input, std::string& err)
{
    if(io_thread) throw std::logic_error("the function sendUpstreamBlocking should not be called in IO thread");
    assert(g_upstreamManager);
    std::promise<Input> promise;
    std::future<Input> future = promise.get_future();
    std::pair< std::promise<Input>, Output> pair = std::make_pair( std::move(promise), output);
    g_upstreamManager->m_promiseQueue->push( std::move(pair) );
    g_upstreamManager->notifyJobReady();
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
        UpstreamSender::Ptr uss = UpstreamSender::Create<UpstreamSender>();
        uss->send(*this, bt);
    }
}

void TaskManager::sendUpstream(BaseTaskPtr bt)
{
    ++m_cntUpstreamSender;
    UpstreamSender::Ptr uss = UpstreamSender::Create();
    uss->send(*this, bt);
}

void TaskManager::onTimer(BaseTaskPtr bt)
{
    assert(dynamic_cast<PeriodicTask*>(bt.get()));
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

    Context::uuid_t uuid = bt->getCtx().getId();
    auto it = m_postponedTasks.find(uuid);
    if (it != m_postponedTasks.end())
        m_postponedTasks.erase(it);

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
            && (m_cntUpstreamSender == m_cntUpstreamSenderDone)
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
        }
        else
        {
            LOG_PRINT_RQS_BT(2,bt,"resuming task with uuid '" << nextUuid << "'.");
            m_readyToResume.push_back(it->second);
            m_postponedTasks.erase(it);
        }
    }
    respondAndDie(bt, bt->getOutput().data());
}

void TaskManager::addPeriodicTask(const Router::Handler3& h3, std::chrono::milliseconds interval_ms)
{
    addPeriodicTask(h3, interval_ms, interval_ms);
}

void TaskManager::addPeriodicTask(
        const Router::Handler3& h3, std::chrono::milliseconds interval_ms, std::chrono::milliseconds initial_interval_ms)
{
    BaseTask* bt = BaseTask::Create<PeriodicTask>(*this, h3, interval_ms, initial_interval_ms).get();
    PeriodicTask* pt = dynamic_cast<PeriodicTask*>(bt);
    assert(pt);
    schedule(pt);
}

TaskManager *TaskManager::from(mg_mgr *mgr)
{
    void* user_data = getUserData(mgr);
    assert(user_data);
    return static_cast<TaskManager*>(user_data);
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

void TaskManager::initThreadPool(int threadCount, int workersQueueSize)
{
    if(threadCount <= 0) threadCount = std::thread::hardware_concurrency();
    if(workersQueueSize <= 0) workersQueueSize = 32;

    tp::ThreadPoolOptions th_op;
    th_op.setThreadCount(threadCount);
    th_op.setQueueSize(workersQueueSize);
    graft::ThreadPoolX thread_pool(th_op);

    size_t resQueueSize;
    {//nearest ceiling power of 2
        size_t val = th_op.threadCount()*th_op.queueSize();
        size_t bit = 1;
        for(; bit<val; bit <<= 1);
        resQueueSize = bit;
    }

    const size_t maxinputSize = th_op.threadCount()*th_op.queueSize();
    graft::TPResQueue resQueue(resQueueSize);

    m_threadPool = std::make_unique<ThreadPoolX>(std::move(thread_pool));
    m_resQueue = std::make_unique<TPResQueue>(std::move(resQueue));
    m_threadPoolInputSize = maxinputSize;
    m_promiseQueue = std::make_unique<PromiseQueue>(threadCount);

    LOG_PRINT_L1("Thread pool created with " << threadCount
                 << " workers with " << workersQueueSize
                 << " queue size each. The output queue size is " << resQueueSize);
}

void TaskManager::setIOThread(bool current)
{
    if(current)
    {
        io_thread = true;
        assert(!g_upstreamManager);
        g_upstreamManager = this;
    }
    else
    {
        g_upstreamManager = nullptr;
        io_thread = false;
    }
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

        ++m_cntUpstreamSenderDone;
        return;
    }
    //here you can send a job to the thread pool or send response to client
    //uss will be destroyed on exit, save its result
    {//now always create a job and put it to the thread pool after CryptoNode
        LOG_PRINT_RQS_BT(2,bt, "CryptoNode answered : '" << make_dump_output( bt->getInput().body, getCopts().log_trunc_to_size ) << "'");
        if(!bt->getSelf())
        {//it is possible that a client has closed connection already
            ++m_cntUpstreamSenderDone;
            return;
        }
        Execute(bt);
    }
    ++m_cntUpstreamSenderDone;
    //uss will be destroyed on exit
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
    this->m_manager.schedule(this);
}

std::chrono::milliseconds PeriodicTask::getTimeout()
{
    auto ret = (m_initial_run) ? m_initial_timeout_ms : m_timeout_ms;
    m_initial_run = false;
    return ret;
}

ClientTask::ClientTask(ConnectionManager* connectionManager, mg_connection *client, Router::JobParams& prms)
    : BaseTask(*TaskManager::from( getMgr(client) ), prms)
    , m_connectionManager(connectionManager)
    , m_client(client)
{
}

void ClientTask::finalize()
{
    releaseItself();
}

}//namespace graft
