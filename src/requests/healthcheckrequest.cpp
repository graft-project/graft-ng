#include "healthcheckrequest.h"

namespace graft {

Status healthcheckHandler(const Router::vars_t& vars, const graft::Input& input,
                          graft::Context& ctx, graft::Output& output)
{
    return Status::Ok;
}

void registerHealthcheckRequest(Router &router)
{
    Router::Handler3 h3(nullptr, healthcheckHandler, nullptr);
    router.addRoute("/health", METHOD_GET, h3);
}

}
