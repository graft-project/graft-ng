#include <string.h>

#include "graft_manager.h"
#include "router.h"

namespace graft {

#ifdef OPT_BUILD_TESTS
std::atomic_bool GraftServer::ready;
#endif


void Manager::sendCrypton(ClientRequest_ptr cr)
{
    ++m_cntCryptoNodeSender;
    CryptoNodeSender::Ptr cns = CryptoNodeSender::Create();
    std::string s = cr->get_input().get();
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
    gj.get_cr()->onJobDone(gj);
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

bool Manager::addRouter(Router& r)
{
    bool armed = r.arm();
    if (armed) m_routers.push_back(r);

    return armed;
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
        m_result.load(s);
        if(!ok) break;
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
    switch(m_status)
    {
    case Router::Status::Ok: code = 200; break;
    case Router::Status::Error: code = 500; break;
    case Router::Status::Drop: code = 400; break;
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
    if(m_prms.h3.pre)
    {
        m_prms.h3.pre(m_prms.vars, m_prms.input, m_ctx, m_output);
        m_prms.input.assign(m_output);
    }
    manager.get_threadPool().post(
                GJ_ptr( get_itself(), &manager.get_resQueue(), &manager )
                );
}

void ClientRequest::onJobDone(GJ &gj)
{
    if(Router::Status::Forward == m_status || m_prms.h3.post)
    {
        m_prms.input.assign(m_output);
    }
    if(m_prms.h3.post)
    {
        m_prms.h3.post(m_prms.vars, m_prms.input, m_ctx, m_output);
        m_prms.input.assign(m_output);
    }
    //here you can send a request to cryptonode or send response to client
    //gj will be destroyed on exit, save its result
    //now it sends response to client
    switch(m_status)
    {
    case Router::Status::Forward:
    {
        assert(m_client);
        Manager::from(m_client)->sendCrypton(get_itself());
    } break;
    case Router::Status::Ok:
    {
        respondToClientAndDie("Job done.");
    } break;
    case Router::Status::Error:
    {
        respondToClientAndDie("Job done with error.");
    } break;
    case Router::Status::Drop:
    {
        respondToClientAndDie("Job done Drop.");
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
    //now it sends response to client
    {//now always create a job and put it to the thread pool after CryptoNode
        //set output of CryptoNode as input for job
        m_prms.input.assign(cns.get_result());
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

constexpr std::pair<const char *, int> GraftServer::m_methods[];

void GraftServer::serve(mg_mgr *mgr)
{
    ServerOpts& opts = Manager::from(mgr)->get_opts();

    mg_connection *nc_http = mg_bind(mgr, opts.http_address.c_str(), ev_handler_http),
                  *nc_coap = mg_bind(mgr, opts.coap_address.c_str(), ev_handler_coap);

    mg_set_protocol_http_websocket(nc_http);
    mg_set_protocol_coap(nc_coap);

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
        if(uri == "/root/exit")
        {
            manager->exit = true;
            return;
        }
        int method = translateMethod(hm->method.p, hm->method.len);
	if (method < 0) return;

        Router::JobParams prms;
        if (Router::match(uri, method, prms))
        {
            mg_str& body = hm->body;
            prms.input.load(body.p, body.len) ;
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

void GraftServer::ev_handler_coap(mg_connection *client, int ev, void *ev_data)
{
    uint32_t res;
    struct mg_coap_message *cm = (struct mg_coap_message *) ev_data;
    Manager* manager = Manager::from(client);
    std::string uri;

    if (cm->code_class != MG_COAP_CODECLASS_REQUEST) return;

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

        int method = translateMethod(cm->code_detail);

        Router::JobParams prms;
        if (Router::match(uri, method, prms))
        {
            mg_str& body = cm->payload;
            prms.input.load(body.p, body.len);

            ClientRequest* clr = ClientRequest::Create(client, prms, manager->get_gcm()).get();
            client->user_data = clr;
            client->handler = ClientRequest::static_ev_handler;

            manager->onNewClient(clr->get_itself());
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
