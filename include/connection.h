#pragma once

#include "task.h"

namespace graft {

template<typename C>
void static_ev_handler(mg_connection *nc, int ev, void *ev_data)
{
    static bool entered = false;
    assert(!entered); //recursive calls are dangerous
    entered = true;
    C* This = static_cast<C*>(nc->user_data);
    assert(This);
    This->ev_handler(nc, ev, ev_data);
    entered = false;
}

void static_empty_ev_handler(mg_connection *nc, int ev, void *ev_data);

class UpstreamSender : public SelfHolder<UpstreamSender>
{
public:
    UpstreamSender(Dummy&) { }

    BaseTaskPtr& getTask() { return m_bt; }

    void send(TaskManager& manager, BaseTaskPtr bt);
    Status getStatus() const { return m_status; }
    const std::string& getError() const { return m_error; }
public:
    void ev_handler(mg_connection* crypton, int ev, void *ev_data);
private:
    void setError(Status status, const std::string& error = std::string())
    {
        m_status = status;
        m_error = error;
    }
private:
    mg_connection *m_crypton = nullptr;
    BaseTaskPtr m_bt;
    Status m_status = Status::None;
    std::string m_error;
};

class ConnectionManager
{
protected:
    static ConnectionManager* from_accepted(mg_connection* cn);
public:
    ConnectionManager() { }

    virtual void bind(TaskManager& manager) = 0;
    virtual void respond(ClientTask* ct, const std::string& s);

    void addRouter(Router& r) { m_root.addRouter(r); }
    bool enableRouting() { return m_root.arm(); }
    bool matchRoute(const std::string& target, int method, Router::JobParams& params) { return m_root.match(target, method, params); }

    std::string dbgDumpRouters() const { return m_root.dbgDumpRouters(); }
    void dbgDumpR3Tree(int level = 0) const { return m_root.dbgDumpR3Tree(level); }
    //returns conflicting endpoint
    std::string dbgCheckConflictRoutes() const { return m_root.dbgCheckConflictRoutes(); }

protected:
    static void ev_handler_empty(mg_connection *client, int ev, void *ev_data);
#define _M(x) std::make_pair(#x, METHOD_##x)
    constexpr static std::pair<const char *, int> m_methods[] = {
        _M(GET), _M(POST), _M(PUT), _M(DELETE), _M(HEAD) //, _M(CONNECT)
    };
protected:
    Router::Root m_root;
};

class HttpConnectionManager final : public ConnectionManager
{
    static HttpConnectionManager* from_accepted(mg_connection* cn)
    {
        ConnectionManager* cm = ConnectionManager::from_accepted(cn);
        assert(dynamic_cast<HttpConnectionManager*>(cm));
        return static_cast<HttpConnectionManager*>(cm);
    }
public:
    HttpConnectionManager() { }
    void bind(TaskManager& manager) override;

private:
    static void ev_handler_http(mg_connection *client, int ev, void *ev_data);
    static int translateMethod(const char *method, std::size_t len);
};

class CoapConnectionManager final : public ConnectionManager
{
    static CoapConnectionManager* from_accepted(mg_connection* cn)
    {
        ConnectionManager* cm = ConnectionManager::from_accepted(cn);
        assert(dynamic_cast<CoapConnectionManager*>(cm));
        return static_cast<CoapConnectionManager*>(cm);
    }
public:
    CoapConnectionManager() { }
    void bind(TaskManager& manager) override;

private:
    static void ev_handler_empty(mg_connection *client, int ev, void *ev_data);
    static void ev_handler_coap(mg_connection *client, int ev, void *ev_data);
    static int translateMethod(int i);

};

}//namespace graft

