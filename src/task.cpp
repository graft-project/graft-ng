#include <string.h>

#include "task.h"
#include "connection.h"
#include "router.h"
#include "log.h"
#include <sstream>

namespace graft {

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

void TaskManager::respondAndDie(BaseTaskPtr bt, const std::string& s)
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
    bt->finalize();
}

void TaskManager::schedule(PeriodicTask* pt)
{
    m_timerList.push(pt->m_timeout_ms, pt->getSelf());
}

void TaskManager::Execute(BaseTaskPtr bt)
{
    assert(m_cntJobDone <= m_cntJobSent);
    if(m_cntJobSent - m_cntJobDone == m_threadPoolInputSize)
    {//check overflow
        bt->getCtx().local.setError("Service Unavailable", Status::Busy);
        respondAndDie(bt,"Thread pool overflow");
        return;
    }
    assert(m_cntJobSent - m_cntJobDone < m_threadPoolInputSize);
    ++m_cntJobSent;

    auto& params = bt->getParams();

    ExecutePreAction(bt);
    if(params.h3.pre_action && Status::Ok != bt->getLastStatus() && Status::Forward != bt->getLastStatus())
    {
        processResult(bt);
        return;
    }
    if(params.h3.worker_action)
    {
        getThreadPool().post(
                    GJPtr( bt, &getResQueue(), this ),
                    true
                    );
    }
    else
    {
        //special case when worker_action is absent
        ExecutePostAction(bt, nullptr);
        processResult(bt);
        //next call is required to fix counters that prevents overflow
        ++m_cntJobDone;
    }
}

bool TaskManager::tryProcessReadyJob()
{
    GJPtr gj;
    bool res = getResQueue().pop(gj);
    if(!res) return res;
    BaseTaskPtr bt = gj->getTask();
    ExecutePostAction(bt, &*gj);
    processResult(bt);
    ++m_cntJobDone;
    return true;
}

void TaskManager::ExecutePreAction(BaseTaskPtr bt)
{
    auto& params = bt->getParams();
    if(!params.h3.pre_action) return;
    auto& ctx = bt->getCtx();
    auto& output = bt->getOutput();

    try
    {
        Status status = params.h3.pre_action(params.vars, params.input, ctx, output);
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
    }
    catch(...)
    {
        bt->setError("unknown exception");
        params.input.reset();
    }
    LOG_PRINT_RQS_BT(3,bt,"pre_action completed with result " << bt->getStrStatus());
}

void TaskManager::ExecutePostAction(BaseTaskPtr bt, GJ* gj)
{
    if(gj)
    {
        LOG_PRINT_RQS_BT(2,bt,"worker_action completed with result " << bt->getStrStatus());
    }
    //post_action if not empty, will be called in any case, even if worker_action results as some kind of error or exception.
    //But, in case pre_action finishes as error both worker_action and post_action will be skipped.
    //post_action has a chance to fix result of pre_action. In case of error was before it it should just return that error.
    auto& params = bt->getParams();
    if(!params.h3.post_action) return;
    auto& ctx = bt->getCtx();
    auto& output = bt->getOutput();

    try
    {
        Status status = params.h3.post_action(params.vars, params.input, ctx, output);
        bt->setLastStatus(status);
        if(Status::Forward == status)
        {
            params.input.assign(output);
        }
    }
    catch(const std::exception& e)
    {
        bt->setError(e.what());
        params.input.reset();
    }
    catch(...)
    {
        bt->setError("unknown exception");
        params.input.reset();
    }
    LOG_PRINT_RQS_BT(3,bt,"post_action completed with result " << bt->getStrStatus());
}

void TaskManager::processResult(BaseTaskPtr bt)
{
    switch(bt->getLastStatus())
    {
    case Status::Forward:
    {
        LOG_PRINT_RQS_BT(3,bt,"Sending request to CryptoNode");
        sendUpstream(bt);
    } break;
    case Status::Ok:
    {
        respondAndDie(bt, bt->getOutput().data());
    } break;
    case Status::InternalError:
    case Status::Error:
    case Status::Stop:
    {
        respondAndDie(bt, bt->getOutput().data());
    } break;
    case Status::Drop:
    {
        respondAndDie(bt, "Job done Drop."); //TODO: Expect HTTP Error Response
    } break;
    default:
    {
        assert(false);
    } break;
    }
}

void TaskManager::addPeriodicTask(const Router::Handler3& h3, std::chrono::milliseconds interval_ms)
{
    BaseTask* bt = BaseTask::Create<PeriodicTask>(*this, h3, interval_ms).get();
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

    LOG_PRINT_L1("Thread pool created with " << threadCount
                 << " workers with " << workersQueueSize
                 << " queue size each. The output queue size is " << resQueueSize);
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
    if(Status::Ok != uss.getStatus())
    {
        bt->setError(uss.getError().c_str(), uss.getStatus());
        LOG_PRINT_RQS_BT(2,bt, "CryptoNode done with error: " << uss.getError().c_str());
        processResult(bt);
        return;
    }
    //here you can send a job to the thread pool or send response to client
    //uss will be destroyed on exit, save its result
    {//now always create a job and put it to the thread pool after CryptoNode
        LOG_PRINT_RQS_BT(2,bt, "CryptoNode answered ");
        if(!bt->getSelf()) return; //it is possible that a client has closed connection already
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
