#include <string.h>

#include "task.h"
#include "connection.h"
#include "router.h"
#include <sstream>

namespace graft {

void TaskManager::sendCrypton(BaseTaskPtr bt)
{
    ++m_cntUpstreamSender;
    UpstreamSender::Ptr uss = UpstreamSender::Create();
    uss->send(*this, bt);
}

void TaskManager::sendToThreadPool(BaseTaskPtr bt)
{
    assert(m_cntJobDone <= m_cntJobSent);
    if(m_cntJobSent - m_cntJobDone == m_threadPoolInputSize)
    {//check overflow
        onTooBusyBT(bt);
        return;
    }
    assert(m_cntJobSent - m_cntJobDone < m_threadPoolInputSize);
    ++m_cntJobSent;
    createJob(bt);
}

void TaskManager::onTooBusyBT(BaseTaskPtr bt)
{
    bt->m_ctx.local.setError("Service Unavailable", Status::Busy);
    respondAndDieBT(bt,"Thread pool overflow");
}

void TaskManager::onEventBT(BaseTaskPtr bt)
{
    assert(dynamic_cast<PeriodicTask*>(bt.get()));
    onNewClient(bt);
}

void TaskManager::respondAndDieBT(BaseTaskPtr bt, const std::string& s)
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


void TaskManager::createJob(BaseTaskPtr bt)
{
    auto& m_prms = bt->m_prms;
    auto& m_ctx = bt->m_ctx;
    auto& m_output = bt->m_output;

    if(m_prms.h3.pre_action)
    {
        try
        {
            Status status = m_prms.h3.pre_action(m_prms.vars, m_prms.input, m_ctx, m_output);
            bt->setLastStatus(status);
            if(Status::Ok == status && (m_prms.h3.worker_action || m_prms.h3.post_action)
                    || Status::Forward == status)
            {
                m_prms.input.assign(m_output);
            }
        }
        catch(const std::exception& e)
        {
            bt->setError(e.what());
            m_prms.input.reset();
        }
        catch(...)
        {
            bt->setError("unknown exeption");
            m_prms.input.reset();
        }

        if(Status::Ok != bt->getLastStatus() && Status::Forward != bt->getLastStatus())
        {
            processResultBT(bt);
            return;
        }
    }

    if(m_prms.h3.worker_action)
    {
        getThreadPool().post(
                    GJ_ptr( bt, &getResQueue(), this ),
                    true
                    );
    }
    else
    {
        //special case when worker_action is absent
        onJobDoneBT(bt, nullptr);
        //next call is required to fix counters that prevents overflow
        jobDone();
    }
}

void TaskManager::onJobDoneBT(BaseTaskPtr bt, GJ* gj)
{
    auto& m_prms = bt->m_prms;
    auto& m_ctx = bt->m_ctx;
    auto& m_output = bt->m_output;

    //post_action if not empty, will be called in any case, even if worker_action results as some kind of error or exception.
    //But, in case pre_action finishes as error both worker_action and post_action will be skipped.
    //post_action has a chance to fix result of pre_action. In case of error was before it it should just return that error.
    if(m_prms.h3.post_action)
    {
        try
        {
            Status status = m_prms.h3.post_action(m_prms.vars, m_prms.input, m_ctx, m_output);
            bt->setLastStatus(status);
            if(Status::Forward == status)
            {
                m_prms.input.assign(m_output);
            }
        }
        catch(const std::exception& e)
        {
            bt->setError(e.what());
            m_prms.input.reset();
        }
        catch(...)
        {
            bt->setError("unknown exeption");
            m_prms.input.reset();
        }
    }
    //here you can send a request to cryptonode or send response to client
    //gj will be destroyed on exit, save its result
    //now it sends response to client
    processResultBT(bt);
}

void TaskManager::processResultBT(BaseTaskPtr bt)
{
    switch(bt->getLastStatus())
    {
    case Status::Forward:
    {
        sendCrypton(bt);
    } break;
    case Status::Ok:
    {
        respondAndDieBT(bt, bt->m_output.data());
    } break;
    case Status::InternalError:
    case Status::Error:
    case Status::Stop:
    {
        respondAndDieBT(bt, bt->m_output.data());
    } break;
    case Status::Drop:
    {
        respondAndDieBT(bt, "Job done Drop."); //TODO: Expect HTTP Error Response
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

void TaskManager::cb_event(mg_mgr *mgr, uint64_t cnt)
{
    TaskManager::from(mgr)->doWork(cnt);
}

TaskManager *TaskManager::from(mg_mgr *mgr)
{
    assert(mgr->user_data);
    return static_cast<TaskManager*>(mgr->user_data);
}

void TaskManager::onNewClient(BaseTaskPtr bt)
{
    ++m_cntBaseTask;
    sendToThreadPool(bt);
}

void TaskManager::onClientDone(BaseTaskPtr bt)
{
    ++m_cntBaseTaskDone;
}

bool TaskManager::tryProcessReadyJob()
{
    GJ_ptr gj;
    bool res = getResQueue().pop(gj);
    if(!res) return res;
    onJobDone(*gj);
    return true;
}

void TaskManager::processReadyJobBlock()
{
    while(true)
    {
        bool res = tryProcessReadyJob();
        if(res) break;
    }
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

    setThreadPool(std::move(thread_pool), std::move(resQueue), maxinputSize);
}

TaskManager::~TaskManager()
{
    mg_mgr_free(&m_mgr);
}

void TaskManager::serve()
{
    m_ready = true;

    for (;;)
    {
        mg_mgr_poll(&m_mgr, m_copts.timer_poll_interval_ms);
        getTimerList().eval();
        if(stopped()) break;
    }
}

void TaskManager::notifyJobReady()
{
    mg_notify(&m_mgr);
}

void TaskManager::doWork(uint64_t cnt)
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

void TaskManager::jobDone()
{
    ++m_cntJobDone;
}

void TaskManager::onJobDone(GJ& gj)
{
    onJobDoneBT(gj.getTask(), &gj);
    jobDone();
    //gj will be destroyed on exit
}

void TaskManager::onCryptonDone(UpstreamSender& uss)
{
    onCryptonDoneBT(uss.getTask(), uss);
    ++m_cntUpstreamSenderDone;
    //uss will be destroyed on exit
}

void TaskManager::onCryptonDoneBT(BaseTaskPtr bt, UpstreamSender &uss)
{
    if(Status::Ok != uss.getStatus())
    {
        bt->setError(uss.getError().c_str(), uss.getStatus());
        processResultBT(bt);
        return;
    }
    //here you can send a job to the thread pool or send response to client
    //uss will be destroyed on exit, save its result
    {//now always create a job and put it to the thread pool after CryptoNode
        sendToThreadPool(bt);
    }
}

void TaskManager::stop()
{
    assert(!m_stop);
    m_stop = true;
}

void TaskManager::setThreadPool(ThreadPoolX &&tp, TPResQueue &&rq, uint64_t m_threadPoolInputSize_)
{
    m_threadPool = std::unique_ptr<ThreadPoolX>(new ThreadPoolX(std::move(tp)));
    m_resQueue = std::unique_ptr<TPResQueue>(new TPResQueue(std::move(rq)));
    m_threadPoolInputSize = m_threadPoolInputSize_;
}

BaseTask::BaseTask(TaskManager& manager, const Router::JobParams& prms)
    : m_manager(manager)
    , m_prms(prms)
    , m_ctx(manager.getGcm())
{
}

void PeriodicTask::finalize()
{
    if(m_ctx.local.getLastStatus() == Status::Stop)
    {
        releaseItself();
        return;
    }
    this->m_manager.schedule(this);
}

ClientTask::ClientTask(ConnectionManager* connectionManager, mg_connection *client, Router::JobParams& prms, Dummy&)
    : BaseTask(*TaskManager::from(client->mgr), prms)
    , m_connectionManager(connectionManager)
    , m_client(client)
{
}

void ClientTask::finalize()
{
    releaseItself();
}

}//namespace graft
