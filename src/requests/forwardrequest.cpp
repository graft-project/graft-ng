#include "forwardrequest.h"

namespace graft {

void registerForwardRequest(Router &router)
{
    auto forward = [](const graft::Router::vars_t& vars, const graft::Input& input, graft::Context& ctx, graft::Output& output)->graft::Status
    {
        if(ctx.local.getLastStatus() == graft::Status::None)
        {
            auto it = vars.equal_range("forward");
            auto& path = it.first->second;
            if(it.first == vars.end())
            {
                throw std::runtime_error("cannot find 'forward' var");
            }
            if(++it.first != it.second)
            {
                throw std::runtime_error("multiple 'forward' vars found");
            }
            output.body = input.body;
            std::cout << "request path='" << path << "' body='" << output.body << "'\n";
            output.path = path;
//            if(path == "getblocks.bin") dump = true;
            return graft::Status::Forward;
        }
        if(ctx.local.getLastStatus() == graft::Status::Forward)
        {
            output.body = input.body;
            std::cout << "response body='" << output.body << "'\n";
            return graft::Status::Ok;
        }
        return graft::Status::Error;
    };

    //METHOD_GET is required here because some GET requests from the wallet has body
    router.addRoute("/{forward:gethashes.bin|json_rpc|getblocks.bin|gettransactions|sendrawtransaction|getheight|get_transaction_pool_hashes.bin|get_outs.bin}",
                               METHOD_POST|METHOD_GET, graft::Router::Handler3(forward,nullptr,nullptr));
}

}
