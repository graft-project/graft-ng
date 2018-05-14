#include "graft_manager.h"

namespace graft {

#ifdef OPT_BUILD_TESTS
std::atomic_bool GraftServer::ready;
#endif


void Manager::sendCrypton(ClientRequest_ptr cr)
{
    ++m_cntCryptoNodeSender;
    CryptoNodeSender::Ptr cns = CryptoNodeSender::Create();
    auto out = cr->get_output().get();
    std::string s(out.first, out.second);
    cns->send(*this, cr, s);
}

void Manager::sendToThreadPool(ClientRequest_ptr cr)
{
    assert(m_cntJobDone <= m_cntJobSent);
    if(m_cntJobDone - m_cntJobSent == m_threadPoolInputSize)
    {//check overflow
        processReadyJobBlock();
    }
    assert(m_cntJobDone - m_cntJobSent < m_threadPoolInputSize);
    ++m_cntJobSent;
    cr->createJob(*this);
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

void Manager::onNewClient(ClientRequest_ptr cr)
{
    ++m_cntClientRequest;
    sendToThreadPool(cr);
}

void Manager::onClientDone(ClientRequest_ptr cr)
{
    ++m_cntClientRequestDone;
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

void Manager::notifyJobReady()
{
    mg_notify(&m_mgr);
}

void Manager::doWork(uint64_t m_cnt)
{
    //job overflow is possible, and we can pop jobs without notification, thus m_cnt useless
    bool res = true;
    while(res)
    {
        res = tryProcessReadyJob();
        if(!res) break;
    }
}

void Manager::onJobDone(GJ& gj)
{
    gj.get_cr()->onJobDone(&gj);
    ++m_cntJobDone;
    //gj will be destroyed on exit
}

void Manager::onCryptonDone(CryptoNodeSender& cns)
{
    cns.get_cr()->onCryptonDone(cns);
    ++m_cntCryptoNodeSenderDone;
    //cns will be destroyed on exit
}

void Manager::setThreadPool(ThreadPoolX &&tp, TPResQueue &&rq, uint64_t m_threadPoolInputSize_)
{
    m_threadPool = std::unique_ptr<ThreadPoolX>(new ThreadPoolX(std::move(tp)));
    m_resQueue = std::unique_ptr<TPResQueue>(new TPResQueue(std::move(rq)));
    m_threadPoolInputSize = m_threadPoolInputSize_;
}


void CryptoNodeSender::send(Manager &manager, ClientRequest_ptr cr, const std::string &data)
{
    m_cr = cr;
    m_data = data;
    m_crypton = mg_connect(manager.get_mg_mgr(),"localhost:1234", static_ev_handler);
    m_crypton->user_data = this;
    //len + data
    help_send_pstring(m_crypton, m_data);
}

void CryptoNodeSender::help_send_pstring(mg_connection *nc, const std::string &data)
{
    int len = data.size();
    mg_send(nc, &len, sizeof(len));
    mg_send(nc, data.c_str(), data.size());
}

bool CryptoNodeSender::help_recv_pstring(mg_connection *nc, void *ev_data, std::string &data)
{
    int cnt = *(int*)ev_data;
    if(cnt < sizeof(int)) return false;
    mbuf& buf = nc->recv_mbuf;
    int len = *(int*)buf.buf;
    if(len + sizeof(len) < cnt) return false;
    data = std::string(buf.buf + sizeof(len), len);
    mbuf_remove(&buf, len + sizeof(len));
    return true;
}

void CryptoNodeSender::ev_handler(mg_connection *crypton, int ev, void *ev_data)
{
    assert(crypton == this->m_crypton);
    switch (ev)
    {
    case MG_EV_RECV:
    {
        std::string s;
        bool ok = help_recv_pstring(crypton, ev_data, s);
        if(!ok) break;
        m_cr->get_input().load(s.c_str(), s.size());
        crypton->flags |= MG_F_CLOSE_IMMEDIATELY;
        Manager::from(crypton)->onCryptonDone(*this);
        crypton->handler = static_empty_ev_handler;
        releaseItself();
    } break;
    default:
        break;
    }
}



void ClientRequest::respondToClientAndDie(const std::string &s)
{
    int code;
    switch(m_ctx.local.getLastStatus())
    {
    case Status::Ok: code = 200; break;
    case Status::InternalError:
    case Status::Error: code = 500; break;
    case Status::Drop: code = 400; break;
    default: assert(false); break;
    }
    mg_http_send_error(m_client, code, s.c_str());
    m_client->flags |= MG_F_SEND_AND_CLOSE;
    m_client->handler = static_empty_ev_handler;
    m_client = nullptr;
    releaseItself();
}

void ClientRequest::createJob(Manager &manager)
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
        manager.get_threadPool().post(
                GJ_ptr( get_itself(), &manager.get_resQueue(), &manager )
                );
    }
    else
    {
        onJobDone(nullptr);
    }
}

void ClientRequest::onJobDone(GJ* gj)
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

void ClientRequest::processResult()
{
    switch(getLastStatus())
    {
    case Status::Forward:
    {
        assert(m_client);
        Manager::from(m_client)->sendCrypton(get_itself());
    } break;
    case Status::Ok:
    {
        respondToClientAndDie(m_output.data());
    } break;
    case Status::InternalError:
    case Status::Error:
    {
        respondToClientAndDie(m_output.data());
    } break;
    case Status::Drop:
    {
        respondToClientAndDie("Job done Drop."); //TODO: Expect HTTP Error Response
    } break;
    default:
    {
        assert(false);
    } break;
    }
}

void ClientRequest::onCryptonDone(CryptoNodeSender &cns)
{
    //here you can send a job to the thread pool or send response to client
    //cns will be destroyed on exit, save its result
    {//now always create a job and put it to the thread pool after CryptoNode
        Manager::from(m_client)->sendToThreadPool(get_itself());
    }
}

void ClientRequest::ev_handler(mg_connection *client, int ev, void *ev_data)
{
    assert(client == this->m_client);
    switch (ev)
    {
    case MG_EV_CLOSE:
    {
        assert(get_itself());
        if(get_itself()) break;
        Manager::from(client)->onClientDone(get_itself());
        client->handler = static_empty_ev_handler;
        releaseItself();
    } break;
    default:
        break;
    }
}

void GraftServer::serve(mg_mgr *mgr)
{
    ServerOpts& opts = Manager::from(mgr)->get_opts();

    mg_connection* nc = mg_bind(mgr, opts.http_address.c_str(), ev_handler);
    mg_set_protocol_http_websocket(nc);
#ifdef OPT_BUILD_TESTS
    ready = true;
#endif
    for (;;)
    {
        mg_mgr_poll(mgr, 1000);
        if(Manager::from(mgr)->exit) break;
    }
    mg_mgr_free(mgr);
}

void GraftServer::setCryptonodeRPCAddress(const std::string &address)
{
    // TODO implement me
}

void GraftServer::setCryptonodeP2PAddress(const std::string &address)
{
    // TODO implement me
}

int GraftServer::methodFromString(const std::string& method)
{
#define _M(x) std::make_pair(#x, METHOD_##x)
    static const std::pair<std::string, int> methods[] = {
        _M(GET), _M(POST), _M(DELETE), _M(HEAD) //, _M(CONNECT)
    };

    for (const auto& m : methods)
    {
        if (m.first == method)
            return m.second;
    }
}

void GraftServer::ev_handler_empty(mg_connection *client, int ev, void *ev_data)
{
}

void GraftServer::ev_handler(mg_connection *client, int ev, void *ev_data)
{
    Manager* manager = Manager::from(client);

    switch (ev)
    {
    case MG_EV_HTTP_REQUEST:
    {
        mg_set_timer(client, 0);

        struct http_message *hm = (struct http_message *) ev_data;
        std::string uri(hm->uri.p, hm->uri.len);
        if(uri == "/root/exit")
        {
            manager->exit = true;
            return;
        }
        std::string s_method(hm->method.p, hm->method.len);
        int method = methodFromString(s_method);

        Router& router = manager->get_router();
        Router::JobParams prms;
        if(router.match(uri, method, prms))
        {
            mg_str& body = hm->body;
            prms.input.load(body.p, body.len);
            ClientRequest* ptr = ClientRequest::Create(client, prms, manager->get_gcm()).get();
            client->user_data = ptr;
            client->handler = ClientRequest::static_ev_handler;
            manager->onNewClient( ptr->get_itself() );
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
        ServerOpts& opts = manager->get_opts();

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





}//namespace graft
