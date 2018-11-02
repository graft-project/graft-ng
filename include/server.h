#pragma once

#include "serveropts.h"
#include "connection.h"

namespace graftlet {
class GraftletLoader;
} //namespace graftlet

namespace graft {


class GraftServer
{
public:
    GraftServer();
    ~GraftServer();
    GraftServer(const GraftServer&) = delete;
    GraftServer& operator = (const GraftServer&) = delete;

    enum RunRes : int
    {
        SignalShutdown,
        SignalTerminate,
        SignalRestart,
        UnexpectedOk,
    };

    bool init(int argc, const char** argv, ConfigOpts* config);
    RunRes run();
protected:
    virtual bool initConfigOption(int argc, const char** argv, ConfigOpts& configOpts);
    virtual void initConnectionManagers();
    bool ready() const { return m_looper && m_looper->ready(); }
    void stop(bool force = false) { m_looper->stop(force); }
private:
    void initLog(int log_level);
    void initGlobalContext();
    void prepareDataDirAndSupernodes();
    void startSupernodePeriodicTasks();
    void serve();
    static void initSignals();
    void addGlobalCtxCleaner();
    void initGraftlets();
    void addGraftletEndpoints(HttpConnectionManager& httpcm);
    void setHttpRouters(HttpConnectionManager& httpcm);
    void setCoapRouters(CoapConnectionManager& coapcm);
    static void checkRoutes(graft::ConnectionManager& cm);
    ConfigOpts& getCopts() { assert(m_looper); return m_looper->getCopts(); }

    std::unique_ptr<graft::Looper> m_looper;
    std::unique_ptr<graftlet::GraftletLoader> m_graftletLoader;
    std::vector<std::unique_ptr<graft::ConnectionManager>> m_conManagers;
};

}//namespace graft

