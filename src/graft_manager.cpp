#include <string.h>

#include "graft_manager.h"
#include "router.h"
#include <sstream>

namespace graft {

void Manager::sendCrypton(BaseTask_ptr cr)
{
    ++m_cntCryptoNodeSender;
    CryptoNodeSender::Ptr cns = CryptoNodeSender::Create();
    cns->send(*this, cr);
}

void Manager::sendToThreadPool(BaseTask_ptr cr)
{
    assert(m_cntJobDone <= m_cntJobSent);
    if(m_cntJobSent - m_cntJobDone == m_threadPoolInputSize)
    {//check overflow
        cr->onTooBusy();
        return;
    }
    assert(m_cntJobSent - m_cntJobDone < m_threadPoolInputSize);
    ++m_cntJobSent;
    cr->createJob();
}

void Manager::addPeriodicTask(const Router::Handler3& h3, std::chrono::milliseconds interval_ms)
{
    BaseTask* rb = BaseTask::Create<PeriodicTask>(*this, h3, interval_ms).get();
    assert(rb);
}

void Manager::cb_event(mg_mgr *mgr, uint64_t cnt)
{
    Manager::from(mgr)->doWork(cnt);
}

Manager *Manager::from(mg_mgr *mgr)
{
    assert(mgr->user_data);
    return static_cast<Manager*>(mgr->user_data);
}

Manager *Manager::from(mg_connection *cn)
{
    return from(cn->mgr);
}

void Manager::onNewClient(BaseTask_ptr cr)
{
    ++m_cntBaseTask;
    sendToThreadPool(cr);
}

void Manager::onClientDone(BaseTask_ptr cr)
{
    ++m_cntBaseTaskDone;
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

void Manager::initThreadPool(int threadCount, int workersQueueSize)
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

Manager::~Manager()
{
}

void Manager::notifyJobReady()
{
    mg_notify(&m_mgr);
}

void Manager::doWork(uint64_t cnt)
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

void Manager::onJobDone()
{
    ++m_cntJobDone;
}

void Manager::onJobDone(GJ& gj)
{
    gj.get_cr()->onJobDone(&gj);
    onJobDone();
    //gj will be destroyed on exit
}

void Manager::onCryptonDone(CryptoNodeSender& cns)
{
    cns.get_cr()->onCryptonDone(cns);
    ++m_cntCryptoNodeSenderDone;
    //cns will be destroyed on exit
}

void Manager::stop()
{
    this->m_exit = true;
}

bool Manager::stopped() const
{
    return this->m_exit;
}

void Manager::setThreadPool(ThreadPoolX &&tp, TPResQueue &&rq, uint64_t m_threadPoolInputSize_)
{
    m_threadPool = std::unique_ptr<ThreadPoolX>(new ThreadPoolX(std::move(tp)));
    m_resQueue = std::unique_ptr<TPResQueue>(new TPResQueue(std::move(rq)));
    m_threadPoolInputSize = m_threadPoolInputSize_;
}

void CryptoNodeSender::send(Manager &manager, BaseTask_ptr cr)
{
    m_cr = cr;

    const ServerOpts& opts = manager.get_c_opts();
    std::string default_uri = opts.cryptonode_rpc_address.c_str();
    Output& output = cr->get_output();
    std::string url = output.makeUri(default_uri);
    std::string extra_headers = output.combine_headers();
    if(extra_headers.empty())
    {
        extra_headers = "Content-Type: application/json\r\n";
    }
    std::string& body = output.body;
    m_crypton = mg_connect_http(manager.get_mg_mgr(), static_ev_handler, url.c_str(),
                             extra_headers.c_str(),
                             (body.empty())? nullptr : body.c_str()); //last nullptr means GET
    assert(m_crypton);
    m_crypton->user_data = this;
    mg_set_timer(m_crypton, mg_time() + opts.upstream_request_timeout);
}

void CryptoNodeSender::ev_handler(mg_connection *crypton, int ev, void *ev_data)
{
    assert(crypton == this->m_crypton);
    switch (ev)
    {
    case MG_EV_CONNECT:
    {
        int& err = *static_cast<int*>(ev_data);
        if(err != 0)
        {
            std::ostringstream ss;
            ss << "cryptonode connect failed: " << strerror(err);
            setError(Status::Error, ss.str().c_str());
            Manager::from(crypton)->onCryptonDone(*this);
            crypton->handler = static_empty_ev_handler;
            releaseItself();
        }
    } break;
    case MG_EV_HTTP_REPLY:
    {
        mg_set_timer(crypton, 0);
        http_message* hm = static_cast<http_message*>(ev_data);
        m_cr->get_input() = *hm;
        setError(Status::Ok);
        crypton->flags |= MG_F_CLOSE_IMMEDIATELY;
        Manager::from(crypton)->onCryptonDone(*this);
        crypton->handler = static_empty_ev_handler;
        releaseItself();
    } break;
    case MG_EV_CLOSE:
    {
        mg_set_timer(crypton, 0);
        setError(Status::Error, "cryptonode connection unexpectedly closed");
        Manager::from(crypton)->onCryptonDone(*this);
        crypton->handler = static_empty_ev_handler;
        releaseItself();
    } break;
    case MG_EV_TIMER:
    {
        mg_set_timer(crypton, 0);
        setError(Status::Error, "cryptonode request timout");
        crypton->flags |= MG_F_CLOSE_IMMEDIATELY;
        Manager::from(crypton)->onCryptonDone(*this);
        crypton->handler = static_empty_ev_handler;
        releaseItself();
    } break;
    default:
        break;
    }
}

void BaseTask::onTooBusy()
{
    m_ctx.local.setError("Service Unavailable", Status::Busy);
    respondAndDie("Thread pool overflow");
}

void BaseTask::createJob()
{
    if(m_prms.h3.pre_action)
    {
        try
        {
            Status status = m_prms.h3.pre_action(m_prms.vars, m_prms.input, m_ctx, m_output);
            setLastStatus(status);
            if(Status::Ok == status && (m_prms.h3.worker_action || m_prms.h3.post_action)
                    || Status::Forward == status)
            {
                m_prms.input.assign(m_output);
            }
        }
        catch(const std::exception& e)
        {
            setError(e.what());
            m_prms.input.reset();
        }
        catch(...)
        {
            setError("unknown exeption");
            m_prms.input.reset();
        }

        if(Status::Ok != getLastStatus() && Status::Forward != getLastStatus())
        {
            processResult();
            return;
        }
    }

    if(m_prms.h3.worker_action)
    {
        m_manager.get_threadPool().post(
                    GJ_ptr( get_itself(), &m_manager.get_resQueue(), &m_manager ),
                    true
                    );
    }
    else
    {
        //special case when worker_action is absent
        onJobDone(nullptr);
        //next call is required to fix counters that prevents overflow
        m_manager.onJobDone();
    }
}

void BaseTask::onJobDone(GJ* gj)
{
    //post_action if not empty, will be called in any case, even if worker_action results as some kind of error or exception.
    //But, in case pre_action finishes as error both worker_action and post_action will be skipped.
    //post_action has a chance to fix result of pre_action. In case of error was before it it should just return that error.
    if(m_prms.h3.post_action)
    {
        try
        {
            Status status = m_prms.h3.post_action(m_prms.vars, m_prms.input, m_ctx, m_output);
            setLastStatus(status);
            if(Status::Forward == status)
            {
                m_prms.input.assign(m_output);
            }
        }
        catch(const std::exception& e)
        {
            setError(e.what());
            m_prms.input.reset();
        }
        catch(...)
        {
            setError("unknown exeption");
            m_prms.input.reset();
        }
    }
    //here you can send a request to cryptonode or send response to client
    //gj will be destroyed on exit, save its result
    //now it sends response to client
    processResult();
}

void BaseTask::processResult()
{
    switch(getLastStatus())
    {
    case Status::Forward:
    {
        m_manager.sendCrypton(get_itself());
    } break;
    case Status::Ok:
    {
        respondAndDie(m_output.data());
    } break;
    case Status::InternalError:
    case Status::Error:
    case Status::Stop:
    {
        respondAndDie(m_output.data());
    } break;
    case Status::Drop:
    {
        respondAndDie("Job done Drop."); //TODO: Expect HTTP Error Response
    } break;
    default:
    {
        assert(false);
    } break;
    }
}

void BaseTask::onCryptonDone(CryptoNodeSender &cns)
{
    if(Status::Ok != cns.getStatus())
    {
        setError(cns.getError().c_str(), cns.getStatus());
        processResult();
        return;
    }
    //here you can send a job to the thread pool or send response to client
    //cns will be destroyed on exit, save its result
    {//now always create a job and put it to the thread pool after CryptoNode
        m_manager.sendToThreadPool(get_itself());
    }
}

void PeriodicTask::onEvent()
{
    m_manager.onNewClient(get_itself());
}

void PeriodicTask::respondAndDie(const std::string& s)
{
    if(m_ctx.local.getLastStatus() == Status::Stop)
    {
        releaseItself();
        return;
    }
    start();
}
void PeriodicTask::start()
{
    auto& tl = m_manager.get_timerList();
    tl.push(m_timeout_ms, get_itself());
}

void ClientRequest::respondAndDie(const std::string &s)
{
    int code;
    switch(m_ctx.local.getLastStatus())
    {
    case Status::Ok: code = 200; break;
    case Status::InternalError:
    case Status::Error: code = 500; break;
    case Status::Busy: code = 503; break;
    case Status::Drop: code = 400; break;
    default: assert(false); break;
    }
    if(Status::Ok == m_ctx.local.getLastStatus())
    {
        mg_send_head(m_client, code, s.size(), "Content-Type: application/json\r\nConnection: close");
        mg_send(m_client, s.c_str(), s.size());
    }
    else
    {
        mg_http_send_error(m_client, code, s.c_str());
    }
    m_client->flags |= MG_F_SEND_AND_CLOSE;
    m_client->handler = static_empty_ev_handler;
    m_client = nullptr;
    releaseItself();
}

void ClientRequest::ev_handler(mg_connection *client, int ev, void *ev_data)
{
    assert(m_client == client);
    assert(&m_manager == Manager::from(client));
    switch (ev)
    {
    case MG_EV_CLOSE:
    {
        assert(get_itself());
        if(get_itself()) break;
        m_manager.onClientDone(get_itself());
        client->handler = static_empty_ev_handler;
        releaseItself();
    } break;
    default:
        break;
    }
}

constexpr std::pair<const char *, int> GraftServer::m_methods[];

void GraftServer::serve(mg_mgr *mgr)
{
    Manager* manager = Manager::from(mgr);
    const ServerOpts& opts = manager->get_c_opts();

    mg_connection *nc_http = mg_bind(mgr, opts.http_address.c_str(), ev_handler_http),
                  *nc_coap = mg_bind(mgr, opts.coap_address.c_str(), ev_handler_coap);

    mg_set_protocol_http_websocket(nc_http);
    mg_set_protocol_coap(nc_coap);

    m_ready = true;

    for (;;)
    {
        mg_mgr_poll(mgr, opts.timer_poll_interval_ms);
        manager->get_timerList().eval();
        if (Manager::from(mgr)->stopped())
            break;
    }
    mg_mgr_free(mgr);
}

int GraftServer::translateMethod(const char *method, std::size_t len)
{
    for (const auto& m : m_methods)
    {
        if (::strncmp(m.first, method, len) == 0)
            return m.second;
    }
    return -1;
}

int GraftServer::translateMethod(int i)
{
    return m_methods[i].second;
}

void GraftServer::ev_handler_empty(mg_connection *client, int ev, void *ev_data)
{
}

void GraftServer::ev_handler_http(mg_connection *client, int ev, void *ev_data)
{
    Manager* manager = Manager::from(client);

    switch (ev)
    {
    case MG_EV_HTTP_REQUEST:
    {
        mg_set_timer(client, 0);

        struct http_message *hm = (struct http_message *) ev_data;
        std::string uri(hm->uri.p, hm->uri.len);
        // TODO: why this is hardcoded ?
        if(uri == "/root/exit")
        {
            manager->stop();
            return;
        }
        int method = translateMethod(hm->method.p, hm->method.len);
        if (method < 0) return;

        Router::JobParams prms;
        if (manager->matchRoute(uri, method, prms))
        {
            mg_str& body = hm->body;
            prms.input.load(body.p, body.len);
            BaseTask* rb_ptr = BaseTask::Create<ClientRequest>(client, prms).get();
            assert(dynamic_cast<ClientRequest*>(rb_ptr));
            ClientRequest* ptr = static_cast<ClientRequest*>(rb_ptr);

            client->user_data = ptr;
            client->handler = ClientRequest::static_ev_handler;

            manager->onNewClient(ptr->get_itself());
        }
        else
        {
            mg_http_send_error(client, 500, "invalid parameter");
            client->flags |= MG_F_SEND_AND_CLOSE;
        }
        break;
    }
    case MG_EV_ACCEPT:
    {
        const ServerOpts& opts = manager->get_c_opts();

        mg_set_timer(client, mg_time() + opts.http_connection_timeout);
        break;
    }
    case MG_EV_TIMER:
        mg_set_timer(client, 0);
        client->handler = ev_handler_empty; //without this we will get MG_EV_HTTP_REQUEST
        client->flags |= MG_F_CLOSE_IMMEDIATELY;
        break;

    default:
        break;
    }
}

void GraftServer::ev_handler_coap(mg_connection *client, int ev, void *ev_data)
{
    uint32_t res;
    Manager* manager = Manager::from(client);
    std::string uri;
    struct mg_coap_message *cm = (struct mg_coap_message *) ev_data;

    if (ev >= MG_COAP_EVENT_BASE)
      if (!cm || cm->code_class != MG_COAP_CODECLASS_REQUEST) return;

    switch (ev)
    {
    case MG_EV_COAP_CON:
        res = mg_coap_send_ack(client, cm->msg_id);
        // No break
    case MG_EV_COAP_NOC:
    {
        struct mg_coap_option *opt = cm->options;
#define COAP_OPT_URI 11
        for (; opt; opt = opt->next)
        {
            if (opt->number = COAP_OPT_URI)
            {
                uri += "/";
                uri += std::string(opt->value.p, opt->value.len);
            }
        }

        int method = translateMethod(cm->code_detail - 1);

        Router::JobParams prms;
        if (manager->matchRoute(uri, method, prms))
        {
            mg_str& body = cm->payload;
            prms.input.load(body.p, body.len);

            BaseTask* rb_ptr = BaseTask::Create<ClientRequest>(client, prms).get();
            assert(dynamic_cast<ClientRequest*>(rb_ptr));
            ClientRequest* ptr = static_cast<ClientRequest*>(rb_ptr);

            client->user_data = ptr;
            client->handler = ClientRequest::static_ev_handler;

            manager->onNewClient(ptr->get_itself());
        }
        break;
    }
    case MG_EV_COAP_ACK:
    case MG_EV_COAP_RST:
        break;
    default:
        break;
    }
}

}//namespace graft
