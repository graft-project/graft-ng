#include "sendrawtxrequest.h"
#include "requestdefines.h"
#include <misc_log_ex.h>


namespace graft {


Status sendRawTxHandler(const Router::vars_t& vars, const graft::Input& input,
                                 graft::Context& ctx, graft::Output& output)
{
    // call from client
    if (!ctx.local.hasKey(__FUNCTION__)) {
        LOG_PRINT_L2("call from client, forwarding to cryptonode...");

        // just forward input to cryptonode
        SendRawTxRequest req = input.get<SendRawTxRequest>();
        output.load(req);
        ctx.local[__FUNCTION__] = true;
        return Status::Forward;
    } else {
    // response from cryptonode
        LOG_PRINT_L2("response from cryptonode : " << input.toString());
        SendRawTxResponse resp = input.get<SendRawTxResponse>();
        if (resp.status == "OK") { // positive reply
            output.load(resp);
            return Status::Ok;
        } else {
            ErrorResponse ret;
            ret.code = ERROR_INTERNAL_ERROR;
            ret.message = resp.reason;
            output.load(ret);
            return Status::Error;
        }
    }
}

void registerSendRawTxRequest(graft::Router &router)
{
    Router::Handler3 h3(nullptr, sendRawTxHandler, nullptr);
    const char * path = "/cryptonode/sendrawtx";
    router.addRoute(path, METHOD_POST, h3);
    LOG_PRINT_L1("route " << path << " registered");
}

}
