#pragma once

#include "serveropts.h"
#include "connection.h"

namespace graftlet {
class GraftletLoader;
} //namespace graftlet

namespace graft::supernode::system_info { class Counter; }

namespace graft {

using SysInfoCounter = supernode::system_info::Counter;

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
    virtual void initRouters();

    bool ready() const;
    void stop(bool force = false);
    ConnectionManager* getConMgr(const ConnectionManager::Proto& proto);

    //the order of members is important because of destruction order.
    std::unique_ptr<graft::Looper> m_looper;
private:
    //void initLog(int log_level);
    void initGlobalContext();
    void initConnectionManagers();
    void serve();
    static void initSignals();
    void addGlobalCtxCleaner();
    void create_looper(ConfigOpts& configOpts);
    void create_system_info_counter(void);
    void initGraftlets();
    void initGraftletRouters();
    static void checkRoutes(graft::ConnectionManager& cm);
    ConfigOpts& getCopts();

    std::unique_ptr<graftlet::GraftletLoader> m_graftletLoader;
    std::map<ConnectionManager::Proto, std::unique_ptr<ConnectionManager>> m_conManagers;
    std::unique_ptr<SysInfoCounter> m_sys_info;
};

}//namespace graft

