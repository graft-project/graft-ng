#include "getinforequest.h"
#include "requestdefines.h"
#include <misc_log_ex.h>


namespace graft {

GRAFT_DEFINE_JSON_RPC_RESPONSE(GetInfoResponseJsonRpc, GetInfoResult);


Status getInfoHandler(const Router::vars_t& vars, const graft::Input& input,
                                 graft::Context& ctx, graft::Output& output)
{

    // Flow:
    // 1 -> call from client : we write request to the output and return "Forward" - it will be forwarded to cryptonode
    //      to check if it call from client - we use ctx.local, which is map created only for this client
    // 2 -> response from cryptonode: we parse input, handle response from cryptonode, compose output to client and return Ok
    // -> this will be forwarded to client

    //1. call from client
    //  1. validate input (in not valid, reply error)
    //  2. prepare request to cryptonode, including
    //     2.1 URI (path),
    //     2.2 HTTP method (nice to have, but we can start with POST only
    //     2.3 HTTP body (normally JSON RPC but could be some arbitrary JSON which is not valid JSON RPC)
    //  3. return Forward, which tells framework to forward request to cryptonode
    LOG_PRINT_L2(__FUNCTION__);
    if (!ctx.local.hasKey(__FUNCTION__)) {
        LOG_PRINT_L0("call from client, forwarding to cryptonode...");
        JsonRpcRequestEmpty req;
        req.method = "get_info";
        output.load(req);
        ctx.local[__FUNCTION__] = true;
        return Status::Forward;
    } else {
    // 2. response from cryptonode
        // Suggested flow ;
        //  1. Check if any network errors here (we need to introduce interface for this)
        //  2. If no network errors, read http status code (we need to introduce interface for this)
        //  3. if http status code is ok (200) read body and parse it
        //  4. handle parsed response and prepare reply to the client

        LOG_PRINT_L0("response from cryptonode (input) : " << input.toString());
        LOG_PRINT_L0("response from cryptonode (output) : " << output.data());

        Status status = ctx.local.getLastStatus();
        std::string error = ctx.local.getLastError();

        GetInfoResponseJsonRpc resp = input.get<GetInfoResponseJsonRpc>();

        if (resp.error.code == 0) { // no error, normal reply
            GetInfoResponse ret;
            ret.status = static_cast<uint64_t>(RTAStatus::Success);
            ret.result = resp.result;
            output.load(ret);
            return Status::Ok;
        } else { // error response
            ErrorResponse ret;
            ret.code = ERROR_INTERNAL_ERROR;
            ret.message = resp.error.message;
            output.load(ret);
            return Status::Error;
        }
    }
}




void registerGetInfoRequest(graft::Router &router)
{
    Router::Handler3 h3(nullptr, getInfoHandler, nullptr);
    const char * path = "/cryptonode/getinfo";
    router.addRoute(path, METHOD_GET, h3);
    LOG_PRINT_L1("route " << path << " registered");
}

}
