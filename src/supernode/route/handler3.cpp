
#include "supernode/route/handler3.h"

namespace graft::supernode::route {

Handler3::Handler3(const Handler& pre_action, const Handler& action, const Handler& post_action, const std::string& name)
: pre_action(pre_action)
, worker_action(action)
, post_action(post_action)
, name(name)
{}

Handler3::Handler3(Handler&& pre_action, Handler&& action, Handler&& post_action, std::string&& name)
: pre_action(std::move(pre_action))
, worker_action(std::move(action))
, post_action(std::move(post_action))
, name(std::move(name))
{}

Handler3::Handler3(const Handler& worker_action)
: worker_action(worker_action)
{}

Handler3::Handler3(Handler&& worker_action)
: worker_action(std::move(worker_action))
{}

}

