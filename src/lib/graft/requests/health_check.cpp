
#include "lib/graft/requests/health_check.h"

#undef MONERO_DEFAULT_LOG_CATEGORY
#define MONERO_DEFAULT_LOG_CATEGORY "supernode.healthcheckrequest"

namespace graft::request {

Status healthcheckHandler(const Router::vars_t& vars, const graft::Input& input,
                          graft::Context& ctx, graft::Output& output)
{
    return Status::Ok;
}

void registerHealthcheckRequest(Router& router)
{
    Router::Handler3 h3(nullptr, healthcheckHandler, nullptr);
    router.addRoute("/health", METHOD_GET, h3);
}

}

