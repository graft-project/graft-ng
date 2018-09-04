#pragma once

#include <memory>
#include <vector>

#include "config_opts.h"

namespace graft {

class ConnectionManager;
class HttpConnectionManager;
class CoapConnectionManager;
class Looper;

namespace supernode { class SystemInfoProvider; }
using supernode::SystemInfoProvider;

class GraftServer
{
public:
    GraftServer(void);
    ~GraftServer(void);

    static bool run(int argc, const char** argv);
protected:
    virtual bool initConfigOption(int argc, const char** argv);
    virtual void intiConnectionManagers();
private:
    void initLog(int log_level);
    void initGlobalContext();
    void prepareDataDirAndSupernodes();
    void startSupernodePeriodicTasks();
    bool init(int argc, const char** argv);
    void serve();
    void stop(bool force = false);
    static void initSignals();
    void addGlobalCtxCleaner();
    void setHttpRouters(HttpConnectionManager& httpcm);
    void setCoapRouters(CoapConnectionManager& coapcm);
    static void checkRoutes(graft::ConnectionManager& cm);
    void create_system_info_provider(void);

    ConfigOpts m_configOpts;
    std::unique_ptr<graft::Looper> m_looper;
    std::vector<std::unique_ptr<graft::ConnectionManager>> m_conManagers;
    std::unique_ptr<SystemInfoProvider> m_sys_info;
};

}//namespace graft

