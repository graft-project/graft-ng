#include "router.h"
#include "graft_manager.h"

namespace graft {

    Router::Status coap_test(const Router::vars_t&, const Input&, Context&, Output&)
    {
         return Router::Status::Ok;
    }

    bool setCoapRouters(Manager& m)
    {
        Router coap_router("/coap");
        Router::Handler3 h3_test(coap_test, nullptr, nullptr);

        coap_router.addRoute("/test", METHOD_GET, &h3_test);

	return m.addRouter(coap_router);
    }
}
