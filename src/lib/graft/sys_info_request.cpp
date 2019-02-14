
#include "lib/graft/sys_info_request.h"

#include "lib/graft/context.h"
#include "lib/graft/connection.h"
#include "lib/graft/inout.h"
#include "lib/graft/router.h"
#include "lib/graft/sys_info.h"

#undef MONERO_DEFAULT_LOG_CATEGORY
#define MONERO_DEFAULT_LOG_CATEGORY "supernode.sys_info"

namespace graft::request::system_info {

using Vars = Router::vars_t;
using Input = graft::Input;
using Ctx = graft::Context;
using Output = graft::Output;

Status handler(const Vars& vars, const Input& input, Ctx& ctx, Output& output)
{
    auto& rsi = ctx.handlerAPI()->runtimeSysInfo();

    Response out;
    auto& ri = out.running_info;

    ri.http_request_total     = rsi.http_request_total_cnt();
    ri.http_request_routed    = rsi.http_request_routed_cnt();
    ri.http_request_unrouted  = rsi.http_request_unrouted_cnt();

    ri.http_resp_status_ok    = rsi.http_resp_status_ok_cnt();
    ri.http_resp_status_error = rsi.http_resp_status_error_cnt();
    ri.http_resp_status_drop  = rsi.http_resp_status_drop_cnt();
    ri.http_resp_status_busy  = rsi.http_resp_status_busy_cnt();

    ri.http_req_bytes_raw  = rsi.http_req_bytes_raw_cnt();
    ri.http_resp_bytes_raw = rsi.http_resp_bytes_raw_cnt();

    ri.upstrm_http_req       = rsi.upstrm_http_req_cnt();
    ri.upstrm_http_resp_ok   = rsi.upstrm_http_resp_ok_cnt();
    ri.upstrm_http_resp_err  = rsi.upstrm_http_resp_err_cnt();

    ri.upstrm_http_req_bytes_raw  = rsi.upstrm_http_req_bytes_raw_cnt();
    ri.upstrm_http_resp_bytes_raw = rsi.upstrm_http_resp_bytes_raw_cnt();

    ri.uptime_sec = rsi.system_uptime_sec();

    auto& cfg = out.configuration;
    const ConfigOpts& co = ctx.handlerAPI()->configOpts();

    cfg.config_filename = co.config_filename;
    cfg.http_address = co.http_address;
    cfg.coap_address = co.coap_address;
    cfg.http_connection_timeout = co.http_connection_timeout;
    cfg.upstream_request_timeout = co.upstream_request_timeout;
    cfg.workers_count = co.workers_count;
    cfg.worker_queue_len = co.worker_queue_len;
    cfg.cryptonode_rpc_address = co.cryptonode_rpc_address;
    cfg.timer_poll_interval_ms = co.timer_poll_interval_ms;
    cfg.lru_timeout_ms = co.lru_timeout_ms;
    cfg.graftlet_dirs = co.graftlet_dirs;
    cfg.log_trunc_to_size = co.log_trunc_to_size;

    /*
    cfg.data_dir = co.data_dir;
    cfg.testnet = co.testnet;
    cfg.stake_wallet_name = co.stake_wallet_name;
    cfg.stake_wallet_refresh_interval_ms = co.stake_wallet_refresh_interval_ms;
    cfg.watchonly_wallets_path == co.watchonly_wallets_path;
    cfg.log_level = co.log_level;
    cfg.log_console = co.log_console;
    cfg.log_filename = co.log_filename;
    cfg.log_categories = co.log_categories;
    */

    output.load(out);
    return Status::Ok;
}

void register_request(Router& router)
{
    Router::Handler3 h3(nullptr, handler, nullptr);
    const char* path = "/sys_info";
    router.addRoute(path, METHOD_GET, h3);
}

}

