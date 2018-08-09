#pragma once

#include "connection.h"

namespace graft {

class GraftServer
{
public:
    static bool run(int argc, const char** argv);
protected:
    virtual bool initConfigOption(int argc, const char** argv, ConfigOpts& configOpts);
    virtual void intiConnectionManagers();
private:
    bool init(int argc, const char** argv);
    void serve();
    void stop(bool force = false) { m_looper->stop(force); }

    static void initSignals();
    void addGlobalCtxCleaner();
    void setHttpRouters(HttpConnectionManager& httpcm);
    void setCoapRouters(CoapConnectionManager& coapcm);
    static void checkRoutes(graft::ConnectionManager& cm);
    const ConfigOpts& getCopts() const { assert(m_looper); return m_looper->getCopts(); }

    std::unique_ptr<graft::Looper> m_looper;
    std::vector<std::unique_ptr<graft::ConnectionManager>> m_conManagers;
};

}//namespace graft

