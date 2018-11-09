#pragma once

#include "router.h"

namespace graft
{

class HandlerAPI
{
public:
    virtual void sendUpstreamBlocking(Output& output, Input& input, std::string& err) = 0;
    virtual bool addPeriodicTask(const Router::Handler& h_worker,
                                        std::chrono::milliseconds interval_ms,
                                        std::chrono::milliseconds initial_interval_ms = std::chrono::milliseconds::max()) = 0;
};

}//namespace graft

