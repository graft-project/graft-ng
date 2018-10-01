#pragma once

#include "connection.h"

namespace graftlet {
class GraftletLoader;
} //namespace graftlet

namespace graft {


class GraftServer
{
public:
    bool run(int argc, const char** argv);
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
    bool init(int argc, const char** argv);
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

