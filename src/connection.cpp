#include "connection.h"
#include "mongoosex.h"

#undef MONERO_DEFAULT_LOG_CATEGORY
#define MONERO_DEFAULT_LOG_CATEGORY "supernode.connection"

namespace graft {

std::string client_addr(mg_connection* client)
{
    if(!client) return "disconnected";
    std::ostringstream oss;
    oss << inet_ntoa(client->sa.sin.sin_addr) << ':' << ntohs(client->sa.sin.sin_port);
    return oss.str();
}

void* getUserData(mg_mgr* mgr) { return mgr->user_data; }
void* getUserData(mg_connection* nc) { return nc->user_data; }
mg_mgr* getMgr(mg_connection* nc) { return nc->mgr; }

void static_empty_ev_handler(mg_connection *nc, int ev, void *ev_data)
{

}

constexpr std::pair<const char *, int> ConnectionManager::m_methods[];

void UpstreamSender::send(TaskManager &manager, BaseTaskPtr bt)
{
    m_bt = bt;

    const ConfigOpts& opts = manager.getCopts();
    std::string default_uri = opts.cryptonode_rpc_address.c_str();
    Output& output = bt->getOutput();
    std::string url = output.makeUri(default_uri);
    std::string extra_headers = output.combine_headers();
    if(extra_headers.empty())
    {
        extra_headers = "Content-Type: application/json\r\n";
    }
    std::string& body = output.body;
    m_upstream = mg::mg_connect_http_x(manager.getMgMgr(), static_ev_handler<UpstreamSender>, url.c_str(),
                             extra_headers.c_str(),
                             body); //body.empty() means GET
    assert(m_upstream);
    m_upstream->user_data = this;
    mg_set_timer(m_upstream, mg_time() + opts.upstream_request_timeout);
}

void UpstreamSender::ev_handler(mg_connection *upstream, int ev, void *ev_data)
{
    assert(upstream == this->m_upstream);
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
            TaskManager::from(upstream->mgr)->onUpstreamDone(*this);
            upstream->handler = static_empty_ev_handler;
            m_upstream = nullptr;
            releaseItself();
        }
    } break;
    case MG_EV_HTTP_REPLY:
    {
        mg_set_timer(upstream, 0);
        http_message* hm = static_cast<http_message*>(ev_data);
        m_bt->getInput() = *hm;
        setError(Status::Ok);
        upstream->flags |= MG_F_CLOSE_IMMEDIATELY;
        TaskManager::from(upstream->mgr)->onUpstreamDone(*this);
        upstream->handler = static_empty_ev_handler;
        m_upstream = nullptr;
        releaseItself();
    } break;
    case MG_EV_CLOSE:
    {
        mg_set_timer(upstream, 0);
        setError(Status::Error, "cryptonode connection unexpectedly closed");
        TaskManager::from(upstream->mgr)->onUpstreamDone(*this);
        upstream->handler = static_empty_ev_handler;
        m_upstream = nullptr;
        releaseItself();
    } break;
    case MG_EV_TIMER:
    {
        mg_set_timer(upstream, 0);
        setError(Status::Error, "cryptonode request timout");
        upstream->flags |= MG_F_CLOSE_IMMEDIATELY;
        TaskManager::from(upstream->mgr)->onUpstreamDone(*this);
        upstream->handler = static_empty_ev_handler;
        m_upstream = nullptr;
        releaseItself();
    } break;
    default:
        break;
    }
}

Looper::Looper(const ConfigOpts& copts)
    : TaskManager(copts)
    , m_mgr(std::make_unique<mg_mgr>())
{
    mg_mgr_init(m_mgr.get(), this, cb_event);
}


Looper::~Looper()
{
    mg_mgr_free(m_mgr.get());
}

void Looper::serve()
{
    setIOThread(true);

    m_ready = true;
    for (;;)
    {
        mg_mgr_poll(m_mgr.get(), m_copts.timer_poll_interval_ms);
        getTimerList().eval();
        checkUpstreamBlockingIO();
        executePostponedTasks();
        if( stopped() && (m_forceStop || canStop()) ) break;
    }

    setIOThread(false);

    LOG_PRINT_L0("Server shutdown.");
}

void Looper::stop(bool force)
{
    assert(!m_stop && !m_forceStop);
    m_stop = true;
    if(force) m_forceStop = true;
}

void Looper::notifyJobReady()
{
    mg_notify(m_mgr.get());
}

void Looper::cb_event(mg_mgr *mgr, uint64_t cnt)
{
    TaskManager::from(mgr)->cb_event(cnt);
}

ConnectionManager* ConnectionManager::from_accepted(mg_connection *cn)
{
    assert(cn->user_data);
    return static_cast<ConnectionManager*>(cn->user_data);
}

void ConnectionManager::ev_handler_empty(mg_connection *client, int ev, void *ev_data)
{
}

void ConnectionManager::ev_handler(ClientTask* ct, mg_connection *client, int ev, void *ev_data)
{
    assert(ct->m_client == client);
    assert(&ct->getManager() == TaskManager::from(client->mgr));
    switch (ev)
    {
    case MG_EV_CLOSE:
    {
        ct->m_client->handler = static_empty_ev_handler;
        ct->m_client = nullptr;
    } break;
    default:
        break;
    }
}


void HttpConnectionManager::bind(Looper& looper)
{
    assert(!looper.ready());
    mg_mgr* mgr = looper.getMgMgr();

    const ConfigOpts& opts = looper.getCopts();

    mg_connection *nc_http = mg_bind(mgr, opts.http_address.c_str(), ev_handler_http);
    if(!nc_http) throw std::runtime_error("Cannot bind to " + opts.http_address);
    nc_http->user_data = this;
    mg_set_protocol_http_websocket(nc_http);
}

void CoapConnectionManager::bind(Looper& looper)
{
    assert(!looper.ready());
    mg_mgr* mgr = looper.getMgMgr();

    const ConfigOpts& opts = looper.getCopts();

    mg_connection *nc_coap = mg_bind(mgr, opts.coap_address.c_str(), ev_handler_coap);
    if(!nc_coap) throw std::runtime_error("Cannot bind to " + opts.coap_address);
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

HttpConnectionManager* HttpConnectionManager::from_accepted(mg_connection* cn)
{
    ConnectionManager* cm = ConnectionManager::from_accepted(cn);
    assert(dynamic_cast<HttpConnectionManager*>(cm));
    return static_cast<HttpConnectionManager*>(cm);
}

CoapConnectionManager* CoapConnectionManager::from_accepted(mg_connection* cn)
{
    ConnectionManager* cm = ConnectionManager::from_accepted(cn);
    assert(dynamic_cast<CoapConnectionManager*>(cm));
    return static_cast<CoapConnectionManager*>(cm);
}

void HttpConnectionManager::ev_handler_http(mg_connection *client, int ev, void *ev_data)
{
    TaskManager* manager = TaskManager::from(client->mgr);

    switch (ev)
    {
    case MG_EV_HTTP_REQUEST:
    {
        mg_set_timer(client, 0);

        struct http_message *hm = (struct http_message *) ev_data;
        std::string uri(hm->uri.p, hm->uri.len);

        int method = translateMethod(hm->method.p, hm->method.len);
        if (method < 0) return;

        std::string s_method(hm->method.p, hm->method.len);
        LOG_PRINT_CLN(1,client,"New HTTP client. uri:" << std::string(hm->uri.p, hm->uri.len) << " method:" << s_method);

        HttpConnectionManager* httpcm = HttpConnectionManager::from_accepted(client);
        Router::JobParams prms;
        if (httpcm->matchRoute(uri, method, prms))
        {
            mg_str& body = hm->body;
            prms.input.load(body.p, body.len);
            LOG_PRINT_CLN(2,client,"Matching Route found; body = " << std::string(body.p, body.len));
            BaseTask* bt = BaseTask::Create<ClientTask>(httpcm, client, prms).get();
            assert(dynamic_cast<ClientTask*>(bt));
            ClientTask* ptr = static_cast<ClientTask*>(bt);

            client->user_data = ptr;
            client->handler = static_ev_handler<ClientTask>;

            manager->onNewClient(ptr->getSelf());
        }
        else
        {
            LOG_PRINT_CLN(2,client,"Matching Route not found; closing connection");
            mg_http_send_error(client, 500, "invalid parameter");
            client->flags |= MG_F_SEND_AND_CLOSE;
        }
        break;
    }
    case MG_EV_ACCEPT:
    {
        const ConfigOpts& opts = manager->getCopts();

        mg_set_timer(client, mg_time() + opts.http_connection_timeout);
        break;
    }
    case MG_EV_TIMER:
    {
        LOG_PRINT_CLN(1,client,"Client timeout; closing connection");
        mg_set_timer(client, 0);
        client->handler = ev_handler_empty; //without this we will get MG_EV_HTTP_REQUEST
        client->flags |= MG_F_CLOSE_IMMEDIATELY;
        break;
    }
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

        CoapConnectionManager* coapcm = CoapConnectionManager::from_accepted(client);
        Router::JobParams prms;
        if (coapcm->matchRoute(uri, method, prms))
        {
            mg_str& body = cm->payload;
            prms.input.load(body.p, body.len);

            BaseTask* rb_ptr = BaseTask::Create<ClientTask>(coapcm, client, prms).get();
            assert(dynamic_cast<ClientTask*>(rb_ptr));
            ClientTask* ptr = static_cast<ClientTask*>(rb_ptr);

            client->user_data = ptr;
            client->handler = static_ev_handler<ClientTask>;

            TaskManager* manager = TaskManager::from(client->mgr);
            manager->onNewClient(ptr->getSelf());
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

void ConnectionManager::respond(ClientTask* ct, const std::string& s)
{
    if(ct->m_client == nullptr)
    {//it is possible that a client has closed connection already
        if(ct->getLastStatus() != Status::Again)
            ct->getManager().onClientDone(ct->getSelf());
        return;
    }
    int code;
    switch(ct->getCtx().local.getLastStatus())
    {
    case Status::Again:
    case Status::Ok: code = 200; break;
    case Status::InternalError:
    case Status::Error: code = 500; break;
    case Status::Busy: code = 503; break;
    case Status::Drop: code = 400; break;
    default: assert(false); break;
    }

    auto& ctx = ct->getCtx();
    auto& client = ct->m_client;
    LOG_PRINT_CLN(2, ct->m_client, "Reply to client: " << s);
    if(Status::Ok == ctx.local.getLastStatus())
    {
        mg_send_head(client, code, s.size(), "Content-Type: application/json\r\nConnection: close");
        mg_send(client, s.c_str(), s.size());
    }
    else
    {
        mg_http_send_error(client, code, s.c_str());
    }
    LOG_PRINT_CLN(2,ct->m_client,"Client request finished with result " << ct->getStrStatus());
    client->flags |= MG_F_SEND_AND_CLOSE;
    if(ct->getLastStatus() != Status::Again)
        ct->getManager().onClientDone(ct->getSelf());
    client->handler = static_empty_ev_handler;
    client = nullptr;
}

}//namespace graft
