
#include "requests/system_info.h"

#include "context.h"
#include "inout.h"
#include "router.h"

#undef MONERO_DEFAULT_LOG_CATEGORY
#define MONERO_DEFAULT_LOG_CATEGORY "supernode.systeminforeqeust"

namespace graft { namespace supernode { namespace request { namespace system_info {

using Vars = Router::vars_t;
using Input = graft::Input;
using Ctx = graft::Context;
using Output = graft::Output;

Status handler(const Vars& vars, const Input& input, Ctx& ctx, Output& output)
{

    return Status::Ok;
}

void register_request(Router& router)
{
    Router::Handler3 h3(nullptr, handler, nullptr);
    const char* path = "/systeminfo";
    router.addRoute(path, METHOD_GET, h3);
}

} } } }

