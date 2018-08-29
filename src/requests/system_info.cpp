
#include "requests/system_info.h"

#include "context.h"
#include "./../system_info.h"   // andrew.kibirev: yes, know that it's UGLY and Wrong. It'll be fixed.
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
    vars;
    input;
    graft::supernode::SystemInfoProvider& sip = graft::supernode::get_system_info_provider_from_ctx(ctx);

    Response out;
    auto& ri = out.runningInfo;

    ri.http_request_total_cnt     = sip.http_request_total_cnt();
    ri.http_request_routed_cnt    = sip.http_request_routed_cnt();
    ri.http_request_unrouted_cnt  = sip.http_request_unrouted_cnt();

    ri.system_http_resp_status_ok    = sip.http_resp_status_ok_cnt();
    ri.system_http_resp_status_error = sip.http_resp_status_error_cnt();
    ri.system_http_resp_status_drop  = sip.http_resp_status_drop_cnt();
    ri.system_http_resp_status_busy  = sip.http_resp_status_busy_cnt();

    ri.system_upstrm_http_req_cnt   = sip.upstrm_http_req_cnt();
    ri.system_upstrm_http_resp_cnt  = sip.upstrm_http_resp_cnt();

    ri.system_uptime_sec = sip.system_uptime_sec();

    output.load(out);
    return Status::Ok;
}

void register_request(Router& router)
{
    Router::Handler3 h3(nullptr, handler, nullptr);
    const char* path = "/systeminfo";
    router.addRoute(path, METHOD_GET, h3);
}

} } } }

