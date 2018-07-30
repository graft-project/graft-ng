#pragma once

#include "connection.h"

namespace graft {

class GraftServer
{
public:
    bool init(int argc, const char** argv);
    void serve();
protected:
    virtual bool initConfigOption(int argc, const char** argv);
    virtual void intiConnectionManagers();
private:
    void initSignals();
    void addGlobalCtxCleaner();
    void setHttpRouters(HttpConnectionManager& httpcm);
    void setCoapRouters(CoapConnectionManager& coapcm);
    static void checkRoutes(graft::ConnectionManager& cm);

    ConfigOpts m_configOpts;
    std::unique_ptr<graft::Looper> m_looper;
    std::vector<std::unique_ptr<graft::ConnectionManager>> m_conManagers;
};

}//namespace graft

