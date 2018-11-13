
#include <gtest/gtest.h>

#include "context.h"
#include "serveropts.h"
#include "inout.h"
#include "router.h"
#include "sys_info.h"
#include "sys_info_request.h"

#include <vector>
#include <string>
#include <chrono>

using SysInfoCounter = graft::supernode::system_info::Counter;
using graft::supernode::system_info::Response;
using graft::ConfigOpts;
using graft::GlobalContextMap;

using Ctx = graft::Context;
using Router = graft::RouterT<graft::InHttp, graft::OutHttp>;
using Vars = Router::vars_t;
using Input = graft::Input;
using Output = graft::Output;

const char* req_path = "/sys_info";
const char* req_method = "GET";

TEST(SysInfo, counter_initial_state)
{
    SysInfoCounter sic;
    EXPECT_EQ(sic.http_request_total_cnt(), 0);
    EXPECT_EQ(sic.http_request_routed_cnt(), 0);
    EXPECT_EQ(sic.http_request_unrouted_cnt(), 0);

    EXPECT_EQ(sic.http_resp_status_ok_cnt(), 0);
    EXPECT_EQ(sic.http_resp_status_error_cnt(), 0);
    EXPECT_EQ(sic.http_resp_status_drop_cnt(), 0);
    EXPECT_EQ(sic.http_resp_status_busy_cnt(), 0);

    EXPECT_EQ(sic.http_req_bytes_raw_cnt(), 0);
    EXPECT_EQ(sic.http_resp_bytes_raw_cnt(), 0);

    EXPECT_EQ(sic.upstrm_http_req_cnt(), 0);
    EXPECT_EQ(sic.upstrm_http_resp_ok_cnt(), 0);
    EXPECT_EQ(sic.upstrm_http_resp_err_cnt(), 0);

    EXPECT_EQ(sic.upstrm_http_req_bytes_raw_cnt(), 0);
    EXPECT_EQ(sic.upstrm_http_resp_bytes_raw_cnt(), 0);

    EXPECT_EQ(sic.system_uptime_sec(), 0);
}

TEST(SysInfo, counters_in_action)
{
    SysInfoCounter sic;

    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    EXPECT_EQ(sic.system_uptime_sec(), 1);

    sic.count_http_request_total();
    EXPECT_EQ(sic.http_request_total_cnt(), 1);
    sic.count_http_request_total();
    EXPECT_EQ(sic.http_request_total_cnt(), 2);

    sic.count_http_request_routed();
    EXPECT_EQ(sic.http_request_routed_cnt(), 1);
    sic.count_http_request_routed();
    EXPECT_EQ(sic.http_request_routed_cnt(), 2);

    sic.count_http_request_unrouted();
    EXPECT_EQ(sic.http_request_unrouted_cnt(), 1);
    sic.count_http_request_unrouted();
    EXPECT_EQ(sic.http_request_unrouted_cnt(), 2);

    sic.count_http_resp_status_ok();
    EXPECT_EQ(sic.http_resp_status_ok_cnt(), 1);
    sic.count_http_resp_status_ok();
    EXPECT_EQ(sic.http_resp_status_ok_cnt(), 2);

    sic.count_http_resp_status_error();
    EXPECT_EQ(sic.http_resp_status_error_cnt(), 1);
    sic.count_http_resp_status_error();
    EXPECT_EQ(sic.http_resp_status_error_cnt(), 2);

    sic.count_http_resp_status_drop();
    EXPECT_EQ(sic.http_resp_status_drop_cnt(), 1);
    sic.count_http_resp_status_drop();
    EXPECT_EQ(sic.http_resp_status_drop_cnt(), 2);

    sic.count_http_resp_status_busy();
    EXPECT_EQ(sic.http_resp_status_busy_cnt(), 1);
    sic.count_http_resp_status_busy();
    EXPECT_EQ(sic.http_resp_status_busy_cnt(), 2);

    sic.count_http_req_bytes_raw(0);
    EXPECT_EQ(sic.http_req_bytes_raw_cnt(), 0);
    sic.count_http_req_bytes_raw(1);
    EXPECT_EQ(sic.http_req_bytes_raw_cnt(), 1);
    sic.count_http_req_bytes_raw(1);
    EXPECT_EQ(sic.http_req_bytes_raw_cnt(), 2);
    sic.count_http_req_bytes_raw(8);
    EXPECT_EQ(sic.http_req_bytes_raw_cnt(), 10);

    sic.count_http_resp_bytes_raw(0);
    EXPECT_EQ(sic.http_resp_bytes_raw_cnt(), 0);
    sic.count_http_resp_bytes_raw(1);
    EXPECT_EQ(sic.http_resp_bytes_raw_cnt(), 1);
    sic.count_http_resp_bytes_raw(1);
    EXPECT_EQ(sic.http_resp_bytes_raw_cnt(), 2);
    sic.count_http_resp_bytes_raw(8);
    EXPECT_EQ(sic.http_resp_bytes_raw_cnt(), 10);

    sic.count_upstrm_http_req();
    EXPECT_EQ(sic.upstrm_http_req_cnt(), 1);
    sic.count_upstrm_http_req();
    EXPECT_EQ(sic.upstrm_http_req_cnt(), 2);

    sic.count_upstrm_http_resp_ok();
    EXPECT_EQ(sic.upstrm_http_resp_ok_cnt(), 1);
    sic.count_upstrm_http_resp_ok();
    EXPECT_EQ(sic.upstrm_http_resp_ok_cnt(), 2);

    sic.count_upstrm_http_resp_err();
    EXPECT_EQ(sic.upstrm_http_resp_err_cnt(), 1);
    sic.count_upstrm_http_resp_err();
    EXPECT_EQ(sic.upstrm_http_resp_err_cnt(), 2);

    sic.count_upstrm_http_req_bytes_raw(0);
    EXPECT_EQ(sic.upstrm_http_req_bytes_raw_cnt(), 0);
    sic.count_upstrm_http_req_bytes_raw(1);
    EXPECT_EQ(sic.upstrm_http_req_bytes_raw_cnt(), 1);
    sic.count_upstrm_http_req_bytes_raw(1);
    EXPECT_EQ(sic.upstrm_http_req_bytes_raw_cnt(), 2);
    sic.count_upstrm_http_req_bytes_raw(8);
    EXPECT_EQ(sic.upstrm_http_req_bytes_raw_cnt(), 10);

    sic.count_upstrm_http_resp_bytes_raw(0);
    EXPECT_EQ(sic.upstrm_http_resp_bytes_raw_cnt(), 0);
    sic.count_upstrm_http_resp_bytes_raw(1);
    EXPECT_EQ(sic.upstrm_http_resp_bytes_raw_cnt(), 1);
    sic.count_upstrm_http_resp_bytes_raw(1);
    EXPECT_EQ(sic.upstrm_http_resp_bytes_raw_cnt(), 2);
    sic.count_upstrm_http_resp_bytes_raw(8);
    EXPECT_EQ(sic.upstrm_http_resp_bytes_raw_cnt(), 10);
}

TEST(SysInfo, response_content_initial_and_after_handler)
{
    GlobalContextMap gcm;
    Ctx ctx(gcm);

    SysInfoCounter sic;
    ConfigOpts co;
    co.workers_count = 0;
    co.worker_queue_len = 0;
    co.timer_poll_interval_ms = 0;
    co.lru_timeout_ms = 0;

    ctx.runtime_sys_info(sic);
    ctx.config_opts(co);

    Vars vars;
    Input inp;
    Output otp;

    Router route;
    graft::supernode::system_info::register_request(route);

    Router::Root router;
    router.addRouter(route);

    EXPECT_TRUE(router.arm());

    Router::JobParams jp;
    const int meth_id = METHOD_GET;
    EXPECT_TRUE(router.match(req_path, meth_id, jp));

    jp.h3.worker_action(vars, inp, ctx, otp); // call the target handler
    Response resp = Response::fromJson(otp.body);

    EXPECT_TRUE(resp.version.empty());
    EXPECT_TRUE(resp.configuration.config_filename.empty());
    EXPECT_TRUE(resp.configuration.http_address.empty());
    EXPECT_TRUE(resp.configuration.coap_address.empty());

    EXPECT_EQ(resp.configuration.http_connection_timeout, 0);
    EXPECT_EQ(resp.configuration.upstream_request_timeout, 0);
    EXPECT_EQ(resp.configuration.workers_count, 0);
    EXPECT_EQ(resp.configuration.worker_queue_len, 0);

    EXPECT_TRUE(resp.configuration.cryptonode_rpc_address.empty());

    EXPECT_EQ(resp.configuration.timer_poll_interval_ms, 0);
    EXPECT_EQ(resp.configuration.lru_timeout_ms, 0);

    EXPECT_TRUE(resp.configuration.graftlet_dirs.empty());
    EXPECT_EQ(resp.configuration.testnet, false);

    EXPECT_EQ(resp.configuration.stake_wallet_refresh_interval_ms, 0);

    EXPECT_EQ(resp.running_info.http_request_total, 0);
    EXPECT_EQ(resp.running_info.http_request_routed, 0);
    EXPECT_EQ(resp.running_info.http_request_unrouted, 0);

    EXPECT_EQ(resp.running_info.http_resp_status_ok, 0);
    EXPECT_EQ(resp.running_info.http_resp_status_error, 0);
    EXPECT_EQ(resp.running_info.http_resp_status_drop, 0);
    EXPECT_EQ(resp.running_info.http_resp_status_busy, 0);

    EXPECT_EQ(resp.running_info.http_req_bytes_raw, 0);
    EXPECT_EQ(resp.running_info.http_resp_bytes_raw, 0);

    EXPECT_EQ(resp.running_info.upstrm_http_req, 0);
    EXPECT_EQ(resp.running_info.upstrm_http_resp_ok, 0);
    EXPECT_EQ(resp.running_info.upstrm_http_resp_err, 0);

    EXPECT_EQ(resp.running_info.upstrm_http_req_bytes_raw, 0);
    EXPECT_EQ(resp.running_info.upstrm_http_resp_bytes_raw, 0);

    sic.count_http_request_total();
    sic.count_http_request_routed();
    sic.count_http_request_unrouted();

    sic.count_http_resp_status_ok();
    sic.count_http_resp_status_error();
    sic.count_http_resp_status_drop();
    sic.count_http_resp_status_busy();

    sic.count_http_req_bytes_raw(1);
    sic.count_http_resp_bytes_raw(1);

    sic.count_upstrm_http_req();
    sic.count_upstrm_http_resp_ok();
    sic.count_upstrm_http_resp_err();

    sic.count_upstrm_http_req_bytes_raw(1);
    sic.count_upstrm_http_resp_bytes_raw(1);

    jp.h3.worker_action(vars, inp, ctx, otp); // call the target handler
    resp = Response::fromJson(otp.body);

    EXPECT_EQ(resp.running_info.http_request_total, 1);
    EXPECT_EQ(resp.running_info.http_request_routed, 1);
    EXPECT_EQ(resp.running_info.http_request_unrouted, 1);

    EXPECT_EQ(resp.running_info.http_resp_status_ok, 1);
    EXPECT_EQ(resp.running_info.http_resp_status_error, 1);
    EXPECT_EQ(resp.running_info.http_resp_status_drop, 1);
    EXPECT_EQ(resp.running_info.http_resp_status_busy, 1);

    EXPECT_EQ(resp.running_info.http_req_bytes_raw, 1);
    EXPECT_EQ(resp.running_info.http_resp_bytes_raw, 1);

    EXPECT_EQ(resp.running_info.upstrm_http_req, 1);
    EXPECT_EQ(resp.running_info.upstrm_http_resp_ok, 1);
    EXPECT_EQ(resp.running_info.upstrm_http_resp_err, 1);

    EXPECT_EQ(resp.running_info.upstrm_http_req_bytes_raw, 1);
    EXPECT_EQ(resp.running_info.upstrm_http_resp_bytes_raw, 1);
}

