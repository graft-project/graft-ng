#include "router.h"
#include "graft_manager.h"
#include "requests.h"

namespace graft {

    Status http_test(const Router::vars_t&, const Input&, Context&, Output&)
    {
         std::cout << "blah-blah" << std::endl;
         return Status::Ok;
    }

    void setHttpRouters(Manager& m)
    {
        Router http_router("/dapi/v2.0");
        Router::Handler3 h3_test1(http_test, nullptr, nullptr);
        http_router.addRoute("/test", METHOD_GET, h3_test1);
        registerRTARequests(http_router);
        m.addRouter(http_router);
    }
}
