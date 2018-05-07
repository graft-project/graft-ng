#include "router.h"
#include "graft_manager.h"

namespace graft {

    Router::Status http_test(const Router::vars_t&, const Input&, Context&, Output&)
    {
         return Router::Status::Ok;
    }

    bool setHttpRouters(Manager& m)
    {
        Router http_router("/dapi/v2.0");
        Router::Handler3 h3_test(http_test, nullptr, nullptr);

        http_router.addRoute("/test", METHOD_GET, &h3_test);

	return m.addRouter(http_router);
    }
}
