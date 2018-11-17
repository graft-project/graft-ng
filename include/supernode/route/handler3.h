
#pragma once

#include "supernode/route/data.h"

#include <string>

namespace graft::supernode::route {

struct Handler3
{
public:
    Handler pre_action;
    Handler worker_action;
    Handler post_action;
    std::string name;

    Handler3() = default;
    ~Handler3() = default;

    Handler3(const Handler3&) = default;
    Handler3(Handler3&&) = default;

    Handler3& operator = (const Handler3&) = default;
    Handler3& operator = (Handler3&&) = default;

    Handler3(const Handler& pre_action, const Handler& action, const Handler& post_action, const std::string& name = std::string());
    Handler3(Handler&& pre_action, Handler&& action, Handler&& post_action, std::string&& name = std::string());
    Handler3(const Handler& worker_action);
    Handler3(Handler&& worker_action);
};

}

