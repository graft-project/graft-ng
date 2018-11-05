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
    virtual ~GraftServer();
    GraftServer(const GraftServer&) = delete;
    GraftServer& operator = (const GraftServer&) = delete;

    enum RunRes : int
    {
        SignalShutdown,
        SignalTerminate,
        SignalRestart,
        UnexpectedOk,
    };

    bool init(int argc, const char** argv, ConfigOpts& configOpts);
    RunRes run();
protected:
    virtual bool initConfigOption(int argc, const char** argv, ConfigOpts& configOpts);
    virtual void initMisc(ConfigOpts& configOpts);
    virtual void initConnectionManagers();
    bool ready() const { return m_looper && m_looper->ready(); }
    void stop(bool force = false) { m_looper->stop(force); }

    //the order of members is important because of destruction order.
    std::unique_ptr<graft::Looper> m_looper;
    std::unique_ptr<graftlet::GraftletLoader> m_graftletLoader;
    std::unique_ptr<HttpConnectionManager> httpcm = std::make_unique<HttpConnectionManager>();
    std::unique_ptr<CoapConnectionManager> coapcm = std::make_unique<CoapConnectionManager>();

private:
    void initLog(int log_level);
    void initGlobalContext();
    void prepareDataDirAndSupernodes();
    void serve();
    static void initSignals();
    void addGlobalCtxCleaner();
    void initGraftlets();
    void addGraftletEndpoints();
    static void checkRoutes(graft::ConnectionManager& cm);
    ConfigOpts& getCopts() { assert(m_looper); return m_looper->getCopts(); }

    std::vector<ConnectionManager*> m_conManagers;
};

}//namespace graft

