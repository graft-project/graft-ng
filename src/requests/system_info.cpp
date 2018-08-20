
#include "requests/system_info.h"

#include "context.h"
#include "connection.h"
#include "./../system_info.h"   // andrew.kibirev: yes, know that it's UGLY and Wrong. It'll be fixed.
#include "inout.h"
#include "router.h"

#undef MONERO_DEFAULT_LOG_CATEGORY
#define MONERO_DEFAULT_LOG_CATEGORY "supernode.systeminforeqeust"

//namespace graft { namespace supernode { namespace request { namespace system_info {
namespace graft::supernode::request::system_info {

using Vars = Router::vars_t;
using Input = graft::Input;
using Ctx = graft::Context;
using Output = graft::Output;

Status handler(const Vars& vars, const Input& input, Ctx& ctx, Output& output)
{
    vars;
    input;
    auto& rsi = ctx.runtime_sys_info();

    Response out;
    auto& ri = out.runningInfo;

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
    const ConfigOpts& co = ctx.config_opts();
    cfg.http_address = co.http_address;
    cfg.coap_address = co.coap_address;
    cfg.http_connection_timeout = co.http_connection_timeout;
    cfg.upstream_request_timeout = co.upstream_request_timeout;
    cfg.workers_count = co.workers_count;
    cfg.worker_queue_len = co.worker_queue_len;
    cfg.cryptonode_rpc_address = co.cryptonode_rpc_address;
    cfg.timer_poll_interval_ms = co.timer_poll_interval_ms;
    cfg.data_dir = co.data_dir;
    cfg.lru_timeout_ms = co.lru_timeout_ms;
    cfg.testnet = co.testnet;
    cfg.stake_wallet_name = co.stake_wallet_name;
    cfg.stake_wallet_refresh_interval_ms = co.stake_wallet_refresh_interval_ms;
    cfg.watchonly_wallets_path == co.watchonly_wallets_path;
    cfg.log_level = co.log_level;
    cfg.log_console = co.log_console;
    cfg.log_filename = co.log_filename;

    output.load(out);
    return Status::Ok;
}

void register_request(Router& router)
{
    Router::Handler3 h3(nullptr, handler, nullptr);
    const char* path = "/systeminfo";
    router.addRoute(path, METHOD_GET, h3);
}

} //} } }

