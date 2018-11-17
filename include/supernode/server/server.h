
#pragma once

#include "connection.h"

namespace graftlet { class GraftletLoader; }
namespace graft::supernode::system_info { class Counter; }

namespace graft::supernode::server {

class Config;
using SysInfoCounter = graft::supernode::system_info::Counter;

enum RunResult : int
{
    SignalShutdown,
    SignalTerminate,
    SignalRestart,
    UnexpectedOk,
};

class Server
{
public:
    Server(void);
    virtual ~Server(void);

    Server(const Server&) = delete;
    Server& operator = (const Server&) = delete;

    bool init(Config& cfg);
    RunResult run(void);

protected:
    virtual void initMisc(Config& cfg) {}
    virtual void initRouters(void) {}

    bool ready(void) const;
    void stop(bool force = false);
    ConnectionManager* getConMgr(const ConnectionManager::Proto& proto);

    //the order of members is important because of destruction order.
    std::unique_ptr<graft::Looper> m_looper;

private:
    void serve(void);

    void initGlobalContext(void);
    void initConnectionManagers(void);

    static void initSignals(void);
    void initGraftlets(void);
    void initGraftletRouters(void);

    void create_looper(Config& cfg);
    void create_system_info_counter(void);

    void add_global_ctx_cleaner(void);

    Config& config(void);

private:
    std::unique_ptr<graftlet::GraftletLoader> m_graftletLoader;
    std::map<ConnectionManager::Proto, std::unique_ptr<ConnectionManager>> m_conManagers;
    std::unique_ptr<SysInfoCounter> m_sys_info;
};

}

