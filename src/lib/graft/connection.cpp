
#include "lib/graft/connection.h"
#include "lib/graft/mongoosex.h"
#include "lib/graft/sys_info.h"
#include "lib/graft/graft_exception.h"

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

std::string client_host(mg_connection* client)
{
    if(!client) return "disconnected";
    return inet_ntoa(client->sa.sin.sin_addr);
}

void* getUserData(mg_mgr* mgr) { return mgr->user_data; }
void* getUserData(mg_connection* nc) { return nc->user_data; }
mg_mgr* getMgr(mg_connection* nc) { return nc->mgr; }

void static_empty_ev_handler(mg_connection *nc, int ev, void *ev_data)
{

}

constexpr std::pair<const char *, int> ConnectionManager::m_methods[];

void UpstreamStub::setConnection(mg_connection* upstream)
{
    assert(m_onCloseCallback);
    upstream->user_data = this;
    upstream->handler = static_ev_handler<UpstreamStub>;
}

void UpstreamStub::ev_handler(mg_connection *upstream, int ev, void *ev_data)
{
    switch (ev)
    {
    case MG_EV_CLOSE:
    {
        mg_set_timer(upstream, 0);
        LOG_PRINT_CLN(2,upstream,"Stub connection closed");
        upstream->handler = static_empty_ev_handler;
        m_onCloseCallback(upstream);
    } break;
    default:
    {
        assert(ev == MG_EV_POLL);
    } break;
    }
}


void UpstreamSender::send(TaskManager &manager, const std::string& def_uri)
{
    assert(m_bt);

    const ConfigOpts& opts = manager.getCopts();

    Output& output = m_bt->getOutput();
    std::string url = output.makeUri(def_uri);

    if(m_bt->getCtx().isCallbackSet())
    {
        m_bt->getCtx().resetCallback();
        Context::uuid_t callback_uuid = m_bt->getCtx().getId();
        assert(!callback_uuid.is_nil());
        //add extra header
        unsigned int mg_port = 0;
        {
            std::string uri = opts.http_address;
            assert(!uri.empty());
            mg_str mg_uri{uri.c_str(), uri.size()};
            int res = mg_parse_uri(mg_uri, 0, 0, 0, &mg_port, 0, 0, 0);
            assert(0<=res);
        }
        std::stringstream ss;
        ss << "http://0.0.0.0:" << mg_port << "/callback/" << boost::uuids::to_string(callback_uuid);

        auto it = std::find_if(output.headers.begin(), output.headers.end(), [](auto& v)->bool { v.first == "X-Callback"; } );
        if(it != output.headers.end())
        {
            std::ostringstream oss;
            oss << "X-Callback header exists and will be overwritten. '" << it->second << "' will be replaced by '" << ss.str() << "'";
            if(ClientTask* ct = dynamic_cast<ClientTask*>(m_bt.get()))
            {

                LOG_PRINT_CLN(0, ct->m_client, oss.str());
            }
            else
            {
                LOG_PRINT_L0(oss.str());
            }
            it->second = ss.str();
        }
        else
        {
            output.headers.emplace_back(std::make_pair("X-Callback", ss.str()));
        }
    }
    std::string extra_headers = output.combine_headers();
    if(extra_headers.empty())
    {
        extra_headers = "Content-Type: application/json\r\n";
    }
    std::string& body = output.body;
    if(m_upstream)
    {
        m_upstream->user_data = this;
        m_upstream->handler = static_ev_handler<UpstreamSender>;
    }
    mg_connection* upstream = mg::mg_connect_http_x(m_upstream, manager.getMgMgr(), static_ev_handler<UpstreamSender>, url.c_str(),
                             extra_headers.c_str(),
                             body); //body.empty() means GET
    assert(upstream != nullptr && (m_upstream == nullptr || m_upstream == upstream));
    if(!m_upstream)
    {
        m_upstream = upstream;
        m_upstream->user_data = this;
    }
    mg_set_timer(m_upstream, mg_time() + m_timeout);

    auto& rsi = manager.runtimeSysInfo();
    rsi.count_upstrm_http_req();
    rsi.count_upstrm_http_req_bytes_raw(url.size() + extra_headers.size() + body.size());
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
            upstream->handler = static_empty_ev_handler;
            m_upstream = nullptr;
            m_onDone(*this, m_connectioId, m_upstream);
            releaseItself();
        }
    } break;
    case MG_EV_HTTP_REPLY:
    {
        mg_set_timer(upstream, 0);
        http_message* hm = static_cast<http_message*>(ev_data);
        m_bt->getInput() = Input(*hm, client_host(upstream));

        ConnectionBase* conBase = ConnectionBase::from(upstream->mgr);
        assert(conBase);
        conBase->getSysInfoCounter().count_upstrm_http_resp_bytes_raw(hm->message.len);

        setError(Status::Ok);
        if(!m_keepAlive)
        {
            upstream->flags |= MG_F_CLOSE_IMMEDIATELY;
            upstream->handler = static_empty_ev_handler;
            m_upstream = nullptr;
        }
        m_onDone(*this, m_connectioId, m_upstream);
        releaseItself();
    } break;
    case MG_EV_CLOSE:
    {
        mg_set_timer(upstream, 0);
        setError(Status::Error, "cryptonode connection unexpectedly closed");
        upstream->handler = static_empty_ev_handler;
        m_upstream = nullptr;
        m_onDone(*this, m_connectioId, m_upstream);
        releaseItself();
    } break;
    case MG_EV_TIMER:
    {
        mg_set_timer(upstream, 0);
        setError(Status::Error, "cryptonode request timout");
        upstream->flags |= MG_F_CLOSE_IMMEDIATELY;
        upstream->handler = static_empty_ev_handler;
        m_upstream = nullptr;
        m_onDone(*this, m_connectioId, m_upstream);
        releaseItself();
    } break;
    default:
        break;
    }
}

ConnectionBase::~ConnectionBase()
{
    //m_looper depends on pointer that is held by m_sysInfo.
    //It could be possible that m_looper uses the counters in its dtor.
    //Thus we should ensure that m_looper should be destroyed before m_sysInfo.
    //Following is explicit destruction order to be independent on the members order..
    m_looper.reset();
    m_sysInfo.reset();
}

ConnectionBase* ConnectionBase::from(mg_mgr *mgr)
{
    void* user_data = getUserData(mgr);
    assert(user_data);
    return static_cast<ConnectionBase*>(user_data);
}

void ConnectionBase::loadBlacklist(const ConfigOpts& copts)
{

    const IPFilterOpts& ipfilter = copts.ipfilter;

    assert(!m_blackList);
    m_blackList = BlackList::Create(
                ipfilter.requests_per_sec,
                ipfilter.window_size_sec,
                ipfilter.ban_ip_sec);

    if(!ipfilter.rules_filename.empty())
    {
        LOG_PRINT_L1("Loading blacklist from file " << ipfilter.rules_filename);
        std::string error;
        try
        {
            m_blackList->readRules(ipfilter.rules_filename.c_str());
        }
        catch(std::exception& e)
        {
            error = e.what();
        }
        std::string warns = m_blackList->getWarnings();
        if(!warns.empty())
        {
            LOG_PRINT_L1("Blacklist warnings :\n" << warns);
        }
        if(!error.empty())
        {
            throw graft::exit_error("Cannot load blacklist, '" + error + "'");
        }
    }
}

void ConnectionBase::setSysInfoCounter(std::unique_ptr<SysInfoCounter>& counter)
{
    assert(!m_sysInfo);
    m_sysInfo.swap(counter);
}

void ConnectionBase::createSystemInfoCounter()
{
    if(m_sysInfo) return;
    m_sysInfo = std::make_unique<SysInfoCounter>();
}

void ConnectionBase::createLooper(ConfigOpts& configOpts)
{
    assert(m_sysInfo && !m_looper);
    m_looper = std::make_unique<Looper>(configOpts, *this);
    m_looperReady = true;
}

ConnectionManager* ConnectionBase::getConMgr(const ConnectionManager::Proto& proto)
{
    auto it = m_conManagers.find(proto);
    assert(it != m_conManagers.end());
    return it->second.get();
}

void ConnectionBase::initConnectionManagers()
{
    std::unique_ptr<HttpConnectionManager> httpcm = std::make_unique<HttpConnectionManager>();
    auto res1 = m_conManagers.emplace(httpcm->getProto(), std::move(httpcm));
    assert(res1.second);
    std::unique_ptr<CoapConnectionManager> coapcm = std::make_unique<CoapConnectionManager>();
    auto res2 = m_conManagers.emplace(coapcm->getProto(), std::move(coapcm));
    assert(res2.second);
}

void ConnectionBase::bindConnectionManagers()
{
    for(auto& it : m_conManagers)
    {
        ConnectionManager* cm = it.second.get();
        cm->enableRouting();
        checkRoutes(*cm);
        cm->bind(getLooper());
    }
}

void ConnectionBase::checkRoutes(graft::ConnectionManager& cm)
{//check conflicts in routes
    std::string s = cm.dbgCheckConflictRoutes();
    if(!s.empty())
    {
        std::cout << std::endl << "==> " << cm.getProto() << " manager.dbgDumpRouters()" << std::endl;
        std::cout << cm.dbgDumpRouters();

        //if you really need dump of r3tree uncomment two following lines
        //std::cout << std::endl << std::endl << "==> manager.dbgDumpR3Tree()" << std::endl;
        //manager.dbgDumpR3Tree();

        throw std::runtime_error("Routes conflict found:" + s);
    }
}


Looper::Looper(const ConfigOpts& copts, ConnectionBase& connectionBase)
    : TaskManager(copts, connectionBase.getSysInfoCounter())
    , m_mgr(std::make_unique<mg_mgr>())
{
    mg_mgr_init(m_mgr.get(), &connectionBase, cb_event);
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
        checkPeriodicTaskIO();
        executePostponedTasks();
        expelWorkers();
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
    TaskManager& tm = ConnectionBase::from(mgr)->getLooper();
    tm.cb_event(cnt);
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
    assert(&ct->getManager() == &ConnectionBase::from(client->mgr)->getLooper());
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
    if(!nc_http)
    {
        std::ostringstream oss;
        oss << "Cannot bind to " << opts.http_address << ". Please check if the address is valid and the port is not used.";
        throw exit_error(oss.str());
    }
    nc_http->user_data = this;
    mg_set_protocol_http_websocket(nc_http);
}

void CoapConnectionManager::bind(Looper& looper)
{
    assert(!looper.ready());
    mg_mgr* mgr = looper.getMgMgr();

    const ConfigOpts& opts = looper.getCopts();

    mg_connection *nc_coap = mg_bind(mgr, opts.coap_address.c_str(), ev_handler_coap);
    if(!nc_coap)
    {
        std::ostringstream oss;
        oss << "Cannot bind to " << opts.coap_address << ". Please check if the address is valid and the port is not used.";
        throw exit_error(oss.str());
    }
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
    ConnectionBase* conBase = ConnectionBase::from(client->mgr);

    switch (ev)
    {
    case MG_EV_HTTP_REQUEST:
    {
        conBase->getLooper().runtimeSysInfo().count_http_request_total();

        mg_set_timer(client, 0);

        struct http_message *hm = (struct http_message *) ev_data;

        conBase->getLooper().runtimeSysInfo().count_http_req_bytes_raw(hm->message.len);

        std::string uri(hm->uri.p, hm->uri.len);

        int method = translateMethod(hm->method.p, hm->method.len);
        if (method < 0) return;

        const sockaddr_in& remote_address = client->sa.sin;
        uint16_t remote_port = static_cast<uint16_t>(remote_address.sin_port);
        char remote_address_host_str[INET_ADDRSTRLEN];
        if (!inet_ntop(AF_INET, &(remote_address.sin_addr), remote_address_host_str, sizeof remote_address_host_str))
            *remote_address_host_str = '\0';

        std::string s_method(hm->method.p, hm->method.len);
        LOG_PRINT_CLN(1,client,"New HTTP client. uri:" << std::string(hm->uri.p, hm->uri.len) << " method:" << s_method
            << " remote: " << remote_address_host_str << ":" << remote_port);

        HttpConnectionManager* httpcm = HttpConnectionManager::from_accepted(client);
        Router::JobParams prms;
        if (httpcm->matchRoute(uri, method, prms))
        {
            conBase->getLooper().runtimeSysInfo().count_http_request_routed();

            mg_str& body = hm->body;
            prms.input = Input(*hm, client_host(client));

            prms.input.port = remote_port;

            LOG_PRINT_CLN(2,client,"Matching Route found; body = " << std::string(body.p, body.len));
            BaseTask* bt = BaseTask::Create<ClientTask>(httpcm, client, prms).get();
            assert(dynamic_cast<ClientTask*>(bt));
            ClientTask* ptr = static_cast<ClientTask*>(bt);

            client->user_data = ptr;
            client->handler = static_ev_handler<ClientTask>;

            conBase->getLooper().onNewClient(ptr->getSelf());
        }
        else
        {
            conBase->getLooper().runtimeSysInfo().count_http_request_unrouted();

            LOG_PRINT_CLN(2,client,"Matching Route not found; closing connection");
            mg_http_send_error(client, 500, "invalid parameter");
            client->flags |= MG_F_SEND_AND_CLOSE;
        }
        break;
    }
    case MG_EV_ACCEPT:
    {
        if(!conBase->getBlackList().processIp( client->sa.sin.sin_addr.s_addr ))
        {
            LOG_PRINT_CLN(2,client,"The address is in the black-list; closing connection");
            client->flags |= MG_F_CLOSE_IMMEDIATELY;
            break;
        }

        const ConfigOpts& opts = conBase->getLooper().getCopts();

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

            ConnectionBase* conBase = ConnectionBase::from(client->mgr);
            conBase->getLooper().onNewClient(ptr->getSelf());
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

    int code = 0;
    auto& ctx = ct->getCtx();
    auto& rsi = ct->getManager().runtimeSysInfo();

    switch(ctx.local.getLastStatus())
    {
        case Status::Again:
        case Status::Ok:              { code = 200; rsi.count_http_resp_status_ok(); }    break;
        case Status::InternalError:
        case Status::Error:           { code = 500; rsi.count_http_resp_status_error(); } break;
        case Status::Busy:            { code = 503; rsi.count_http_resp_status_busy(); }  break;
        case Status::Drop:            { code = 400; rsi.count_http_resp_status_drop(); }  break;
        default:                      assert(false);                                      break;
    }

    auto& client = ct->m_client;
    LOG_PRINT_CLN(2, client, "Reply to client: " << s);
    if(Status::Ok == ctx.local.getLastStatus())
    {
        mg_send_head(client, code, s.size(), "Content-Type: application/json\r\nConnection: close");
        mg_send(client, s.c_str(), s.size());
        rsi.count_http_resp_bytes_raw(s.size());
    }
    else
    {
        mg_http_send_error(client, code, s.c_str());
    }

    LOG_PRINT_CLN(2, client, "Client request finished with result " << ct->getStrStatus());
    client->flags |= MG_F_SEND_AND_CLOSE;
    if(ct->getLastStatus() != Status::Again)
        ct->getManager().onClientDone(ct->getSelf());
    client->handler = static_empty_ev_handler;
    client = nullptr;
}

}//namespace graft

