#include "forwardrequest.h"

namespace graft {

void registerForwardRequest(Router &router)
{
    auto forward = [](const graft::Router::vars_t& vars, const graft::Input& input, graft::Context& ctx, graft::Output& output)->graft::Status
    {
        assert(vars.size() == 1);
        assert(vars[0].first == "forward");
        if(ctx.local.getLastStatus() == graft::Status::None)
        {
            output.body = input.body;
            //        output.uri = "$node";
            output.path = vars[0].second;
            return graft::Status::Forward;
        }
        if(ctx.local.getLastStatus() == graft::Status::Forward)
        {
            output.body = input.body;
            return graft::Status::Ok;
        }
        return graft::Status::Error;
    };

    router.addRoute("/{forward:gethashes.bin|json_rpc|getblocks.bin|gettransactions|sendrawtransaction}",
                               METHOD_POST, graft::Router::Handler3(forward,nullptr,nullptr));
}

}
