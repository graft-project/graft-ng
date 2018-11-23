
#pragma once

#include "lib/graft/serveropts.h"
#include "lib/graft/connection.h"

namespace graftlet { class GraftletLoader; }
namespace graft::request::system_info { class Counter; }

namespace graft {

using SysInfoCounter = request::system_info::Counter;

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

    //Call the function with derived SysInfoCounter once only if required.
    //Otherwise, default SysInfoCounter will be created.
    void setSysInfoCounter(std::unique_ptr<SysInfoCounter> counter);
    SysInfoCounter& getSysInfoCounter() { return *m_sysInfo.get(); }

    bool init(int argc, const char** argv, ConfigOpts& configOpts);
    RunRes run();

    void getThreadPoolInfo(uint64_t& activeWorkers, uint64_t& expelledWorkers) const;
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
    void initLog(int log_level);
    void initGlobalContext();
    void initConnectionManagers();
    void addGenericCallbackRoute();
    void serve();
    static void initSignals();
    void addGlobalCtxCleaner();
    void createLooper(ConfigOpts& configOpts);
    void createSystemInfoCounter(void);
    void initGraftlets();
    void initGraftletRouters();
    static void checkRoutes(graft::ConnectionManager& cm);
    ConfigOpts& getCopts();

    std::unique_ptr<graftlet::GraftletLoader> m_graftletLoader;
    std::map<ConnectionManager::Proto, std::unique_ptr<ConnectionManager>> m_conManagers;
    std::unique_ptr<SysInfoCounter> m_sysInfo;
};

}//namespace graft

