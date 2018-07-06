#include <string.h>

#include "graft_manager.h"
#include "router.h"
#include <sstream>

namespace graft {

void static_empty_ev_handler(mg_connection *nc, int ev, void *ev_data)
{

}

void Manager::sendCrypton(BaseTask_ptr cr)
{
    ++m_cntCryptoNodeSender;
    CryptoNodeSender::Ptr cns = CryptoNodeSender::Create();
    cns->send(*this, cr);
}

void Manager::sendToThreadPool(BaseTask_ptr bt)
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

void Manager::onTooBusyBT(BaseTask_ptr bt)
{
    bt->m_ctx.local.setError("Service Unavailable", Status::Busy);
    respondAndDieBT(bt,"Thread pool overflow");
}

void Manager::onEventBT(BaseTask_ptr bt)
{
    assert(dynamic_cast<PeriodicTask*>(bt.get()));
    onNewClient(bt);
}

void Manager::respondAndDieBT(BaseTask_ptr bt, const std::string& s)
{
    ClientRequest* cr = dynamic_cast<ClientRequest*>(bt.get());
    if(cr)
    {
        ConnectionManager::respond(cr, s);
    }
    else
    {
        assert( dynamic_cast<PeriodicTask*>(bt.get()) );
    }
    bt->finalize();
}

void Manager::schedule(PeriodicTask* pt)
{
    m_timerList.push(pt->m_timeout_ms, pt->get_itself());
}


void Manager::createJob(BaseTask_ptr bt)
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
        get_threadPool().post(
                    GJ_ptr( bt, &get_resQueue(), this ),
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

void Manager::onJobDoneBT(BaseTask_ptr bt, GJ* gj)
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

void Manager::processResultBT(BaseTask_ptr bt)
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

void Manager::addPeriodicTask(const Router::Handler3& h3, std::chrono::milliseconds interval_ms)
{
    BaseTask* bt = BaseTask::Create<PeriodicTask>(*this, h3, interval_ms).get();
    PeriodicTask* pt = dynamic_cast<PeriodicTask*>(bt);
    assert(pt);
    schedule(pt);
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
    mg_mgr_free(&m_mgr);
}

void Manager::serve()
{
    m_ready = true;

    for (;;)
    {
        mg_mgr_poll(&m_mgr, m_sopts.timer_poll_interval_ms);
        get_timerList().eval();
        if(stopped()) break;
    }
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

void Manager::jobDone()
{
    ++m_cntJobDone;
}

void Manager::onJobDone(GJ& gj)
{
    onJobDoneBT(gj.get_cr(), &gj);
    jobDone();
    //gj will be destroyed on exit
}

void Manager::onCryptonDone(CryptoNodeSender& cns)
{
    onCryptonDoneBT(cns.get_cr(), cns);
    ++m_cntCryptoNodeSenderDone;
    //cns will be destroyed on exit
}

void Manager::onCryptonDoneBT(BaseTask_ptr bt, CryptoNodeSender &cns)
{
    if(Status::Ok != cns.getStatus())
    {
        bt->setError(cns.getError().c_str(), cns.getStatus());
        processResultBT(bt);
        return;
    }
    //here you can send a job to the thread pool or send response to client
    //cns will be destroyed on exit, save its result
    {//now always create a job and put it to the thread pool after CryptoNode
        sendToThreadPool(bt);
    }
}

void Manager::stop()
{
    assert(!m_stop);
    m_stop = true;
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
    m_crypton = mg_connect_http(manager.get_mg_mgr(), static_ev_handler<CryptoNodeSender>, url.c_str(),
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
            Manager::from(crypton->mgr)->onCryptonDone(*this);
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
        Manager::from(crypton->mgr)->onCryptonDone(*this);
        crypton->handler = static_empty_ev_handler;
        releaseItself();
    } break;
    case MG_EV_CLOSE:
    {
        mg_set_timer(crypton, 0);
        setError(Status::Error, "cryptonode connection unexpectedly closed");
        Manager::from(crypton->mgr)->onCryptonDone(*this);
        crypton->handler = static_empty_ev_handler;
        releaseItself();
    } break;
    case MG_EV_TIMER:
    {
        mg_set_timer(crypton, 0);
        setError(Status::Error, "cryptonode request timout");
        crypton->flags |= MG_F_CLOSE_IMMEDIATELY;
        Manager::from(crypton->mgr)->onCryptonDone(*this);
        crypton->handler = static_empty_ev_handler;
        releaseItself();
    } break;
    default:
        break;
    }
}

BaseTask::BaseTask(Manager& manager, const Router::JobParams& prms)
    : m_manager(manager)
    , m_prms(prms)
    , m_ctx(manager.get_gcm())
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

ClientRequest::ClientRequest(mg_connection *client, Router::JobParams& prms)
    : BaseTask(*Manager::from(client->mgr), prms)
    , m_client(client)
{
}

void ClientRequest::finalize()
{
    releaseItself();
}

void ClientRequest::ev_handler(mg_connection *client, int ev, void *ev_data)
{
    assert(m_client == client);
    assert(&m_manager == Manager::from(client->mgr));
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

constexpr std::pair<const char *, int> ConnectionManager::m_methods[];

ConnectionManager* ConnectionManager::from(mg_connection *cn)
{
    assert(cn->user_data);
    return static_cast<ConnectionManager*>(cn->user_data);
}

void ConnectionManager::ev_handler_empty(mg_connection *client, int ev, void *ev_data)
{
}

void HttpConnectionManager::bind(Manager& manager)
{
    assert(!manager.ready());
    mg_mgr* mgr = manager.get_mg_mgr();

    const ServerOpts& opts = manager.get_c_opts();

    mg_connection *nc_http = mg_bind(mgr, opts.http_address.c_str(), ev_handler_http);
    nc_http->user_data = this;
    mg_set_protocol_http_websocket(nc_http);
}

void CoapConnectionManager::bind(Manager& manager)
{
    assert(!manager.ready());
    mg_mgr* mgr = manager.get_mg_mgr();

    const ServerOpts& opts = manager.get_c_opts();

    mg_connection *nc_coap = mg_bind(mgr, opts.coap_address.c_str(), ev_handler_coap);
    nc_coap->user_data = this;
    mg_set_protocol_coap(nc_coap);
}

int HttpConnectionManager::translateMethod(const char *method, std::size_t len)
{
    for (const auto& m : m_methods)
    {
        if (::strncmp(m.first, method, len) == 0)
            return m.second;
    }
    return -1;
}

int CoapConnectionManager::translateMethod(int i)
{
    constexpr int size = sizeof(m_methods)/sizeof(m_methods[0]);
    assert(i<size);
    return m_methods[i].second;
}

void HttpConnectionManager::ev_handler_http(mg_connection *client, int ev, void *ev_data)
{
    Manager* manager = Manager::from(client->mgr);

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

        HttpConnectionManager* httpcm = HttpConnectionManager::from(client);
        Router::JobParams prms;
        if (httpcm->matchRoute(uri, method, prms))
        {
            mg_str& body = hm->body;
            prms.input.load(body.p, body.len);
            BaseTask* rb_ptr = BaseTask::Create<ClientRequest>(client, prms).get();
            assert(dynamic_cast<ClientRequest*>(rb_ptr));
            ClientRequest* ptr = static_cast<ClientRequest*>(rb_ptr);

            client->user_data = ptr;
            client->handler = static_ev_handler<ClientRequest>;

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

void CoapConnectionManager::ev_handler_coap(mg_connection *client, int ev, void *ev_data)
{
    uint32_t res;
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

        CoapConnectionManager* coapcm = CoapConnectionManager::from(client);
        Router::JobParams prms;
        if (coapcm->matchRoute(uri, method, prms))
        {
            mg_str& body = cm->payload;
            prms.input.load(body.p, body.len);

            BaseTask* rb_ptr = BaseTask::Create<ClientRequest>(client, prms).get();
            assert(dynamic_cast<ClientRequest*>(rb_ptr));
            ClientRequest* ptr = static_cast<ClientRequest*>(rb_ptr);

            client->user_data = ptr;
            client->handler = static_ev_handler<ClientRequest>;

            Manager* manager = Manager::from(client->mgr);
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

void ConnectionManager::respond(ClientRequest* cr, const std::string& s)
{
    int code;
    switch(cr->m_ctx.local.getLastStatus())
    {
    case Status::Ok: code = 200; break;
    case Status::InternalError:
    case Status::Error: code = 500; break;
    case Status::Busy: code = 503; break;
    case Status::Drop: code = 400; break;
    default: assert(false); break;
    }

    auto& m_ctx = cr->m_ctx;
    auto& m_client = cr->m_client;
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
}

}//namespace graft
