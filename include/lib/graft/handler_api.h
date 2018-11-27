#pragma once

#include "lib/graft/router.h"

namespace graft
{

namespace request::system_info { class Counter; }
struct ConfigOpts;

class HandlerAPI
{
public:
    virtual void sendUpstreamBlocking(Output& output, Input& input, std::string& err) = 0;
    virtual bool addPeriodicTask(const Router::Handler& h_worker,
                                        std::chrono::milliseconds interval_ms,
                                        std::chrono::milliseconds initial_interval_ms = std::chrono::milliseconds::max()) = 0;
    virtual request::system_info::Counter& runtimeSysInfo() = 0;
    virtual const ConfigOpts& configOpts() const = 0;
};

}//namespace graft
