
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
    SysInfoCounter& getSysInfoCounter() { assert(m_connectionBase); return m_connectionBase->getSysInfoCounter(); }

    bool init(int argc, const char** argv, ConfigOpts& configOpts);
    RunRes run();

    void getThreadPoolInfo(uint64_t& activeWorkers, uint64_t& expelledWorkers) const;
protected:
    virtual bool initConfigOption(int argc, const char** argv, ConfigOpts& configOpts);
    virtual void initMisc(ConfigOpts& configOpts);
    virtual void initRouters();

    bool ready() const { return m_connectionBaseReady && m_connectionBase->ready(); }
    void stop(bool force = false) { assert(m_connectionBase); m_connectionBase->stop(force); }
    ConnectionManager* getConMgr(const ConnectionManager::Proto& proto) { assert(m_connectionBase); return m_connectionBase->getConMgr(proto); }
    Looper& getLooper() { assert(m_connectionBase); return m_connectionBase->getLooper(); }
    ConnectionBase& getConnectionBase() { assert(m_connectionBase); return *m_connectionBase; }
private:
    void initLog(int log_level);
    void initGlobalContext();
    void prepareDataDir(ConfigOpts& configOpts);
    void addGenericCallbackRoute();
    void serve();
    static void initSignals();
    void addGlobalCtxCleaner();
    void initGraftlets();
    void initGraftletRouters();

    ConfigOpts& getCopts();

    std::unique_ptr<graftlet::GraftletLoader> m_graftletLoader;
    std::atomic_bool m_connectionBaseReady{false};
    std::unique_ptr<ConnectionBase> m_connectionBase;
};

}//namespace graft

