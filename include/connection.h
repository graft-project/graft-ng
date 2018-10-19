#pragma once

#include "task.h"

namespace graft {

namespace details
{

template<typename C>
class default_F
{
public:
    void operator()(C* This, mg_connection *nc, int ev, void *ev_data)
    {
        This->ev_handler(nc, ev, ev_data);
    }
};

} //namespace details

void* getUserData(mg_mgr* mgr);
void* getUserData(mg_connection* nc);
mg_mgr* getMgr(mg_connection* nc);

template<typename C, typename F = details::default_F<C>>
void static_ev_handler(mg_connection *nc, int ev, void *ev_data)
{
    static bool entered = false;
    assert(!entered); //recursive calls are dangerous
    entered = true;
    C* This = static_cast<C*>(getUserData(nc));
    assert(This);
    F f;
    f(This, nc, ev, ev_data);
    entered = false;
}

void static_empty_ev_handler(mg_connection *nc, int ev, void *ev_data);

class UpstreamSender : public SelfHolder<UpstreamSender>
{
public:
    UpstreamSender() = default;

    BaseTaskPtr& getTask() { return m_bt; }

    void send(TaskManager& manager, BaseTaskPtr bt);
    Status getStatus() const { return m_status; }
    const std::string& getError() const { return m_error; }

    void ev_handler(mg_connection* upstream, int ev, void *ev_data);
private:
    void setError(Status status, const std::string& error = std::string())
    {
        m_status = status;
        m_error = error;
    }

    mg_connection *m_upstream = nullptr;
    BaseTaskPtr m_bt;
    Status m_status = Status::None;
    std::string m_error;
};

class Looper final : public TaskManager
{
public:
    Looper(const ConfigOpts& copts);
    virtual ~Looper();

    void serve();
    void notifyJobReady() override;

    void stop(bool force = false);
    bool ready() const { return m_ready; }
    bool stopped() const { return m_stop; }

    virtual mg_mgr* getMgMgr() override { return m_mgr.get(); }
protected:
    std::unique_ptr<mg_mgr> m_mgr;
private:
    ////static functions
    static void cb_event(mg_mgr* mgr, uint64_t cnt);

    std::atomic_bool m_ready {false};
    std::atomic_bool m_stop {false};
    std::atomic_bool m_forceStop {false};
};

class ConnectionManager
{
public:
    virtual void bind(Looper& looper) = 0;
    virtual void respond(ClientTask* ct, const std::string& s);

    ConnectionManager(const std::string& name) : m_name(name) { }
    ConnectionManager(const ConnectionManager&) = delete;
    ConnectionManager& operator = (const ConnectionManager&) = delete;
    virtual ~ConnectionManager() = default;

    void addRouter(Router& r) { m_root.addRouter(r); }
    bool enableRouting() { return m_root.arm(); }
    bool matchRoute(const std::string& target, int method, Router::JobParams& params) { return m_root.match(target, method, params); }

    std::string dbgDumpRouters() const { return m_root.dbgDumpRouters(); }
    void dbgDumpR3Tree(int level = 0) const { return m_root.dbgDumpR3Tree(level); }
    //returns conflicting endpoint
    std::string dbgCheckConflictRoutes() const { return m_root.dbgCheckConflictRoutes(); }
    std::string getName() const { return m_name; }

    static void ev_handler(ClientTask* ct, mg_connection *client, int ev, void *ev_data);
protected:
    static ConnectionManager* from_accepted(mg_connection* cn);
    static void ev_handler_empty(mg_connection *client, int ev, void *ev_data);
#define _M(x) std::make_pair(#x, METHOD_##x)
    constexpr static std::pair<const char *, int> m_methods[] = {
        _M(GET), _M(POST), _M(PUT), _M(DELETE), _M(HEAD) //, _M(CONNECT)
    };

    Router::Root m_root;
private:
    std::string m_name;
};

namespace details
{

template<>
class default_F<ClientTask>
{
public:
    void operator()(ClientTask* This, mg_connection *nc, int ev, void *ev_data)
    {
        This->m_connectionManager->ev_handler(This, nc, ev, ev_data);
    }
};

} //namespace details

class HttpConnectionManager final : public ConnectionManager
{
public:
    HttpConnectionManager() : ConnectionManager("HTTP") { }

    void bind(Looper& looper) override;

    static int translateMethod(const char *method, std::size_t len);
private:
    static void ev_handler_http(mg_connection *client, int ev, void *ev_data);
    static HttpConnectionManager* from_accepted(mg_connection* cn);
};

class WsConnectionManager final : public ConnectionManager
{
public:
    WsConnectionManager() : ConnectionManager("WS") { }
    WsConnectionManager(const WsConnectionManager&) = delete;
    WsConnectionManager* operator = (const WsConnectionManager&) = delete;
    ~WsConnectionManager();

    void bind(Looper& looper) override;

    mg_connection* connect(TaskManager* manager, const Addr& addr);
    void sendFrame(mg_connection* nc, const WsFrame& frame);
    void close(mg_connection* nc);
private:
    static void ev_handler_ws(mg_connection *client, int ev, void *ev_data);
    static WsConnectionManager* from_accepted(mg_connection* cn);
    mg_connection* m_wsListener = nullptr;
};

class CoapConnectionManager final : public ConnectionManager
{
public:
    CoapConnectionManager() : ConnectionManager("COAP") { }

    void bind(Looper& looper) override;

private:
    static void ev_handler_coap(mg_connection *client, int ev, void *ev_data);
    static int translateMethod(int i);
    static CoapConnectionManager* from_accepted(mg_connection* cn);
};

}//namespace graft

