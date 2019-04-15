
#pragma once

#include <string>
#include <vector>
#include <cstdint>

#include "lib/graft/jsonrpc.h"
#include "lib/graft/graftlets_sys_info.h"

namespace graft { template<typename In, typename Out> class RouterT; class InHttp; class OutHttp; using Router = RouterT<InHttp, OutHttp>; }

namespace graft::request::system_info {

using u32 = std::uint32_t;
using u64 = std::uint64_t;

GRAFT_DEFINE_IO_STRUCT_INITED(Configuratioon,
    (std::string, config_filename, std::string()),
    (std::string, http_address, std::string()),
    (std::string, coap_address, std::string()),
    (u32, http_connection_timeout, 0),
    (u32, upstream_request_timeout, 0),
    (u32, workers_count, 0),
    (u32, worker_queue_len, 0),
    (std::string, cryptonode_rpc_address, std::string()),
    (u32, timer_poll_interval_ms, 0),
    (u32, lru_timeout_ms, 0),
    (std::vector<std::string>, graftlet_dirs, std::vector<std::string>()),
    (bool, testnet, false),
    (std::string, data_dir, std::string()),
    (std::string, stake_wallet_name, std::string()),
    (u32, stake_wallet_refresh_interval_ms, 0),
    (std::string, watchonly_wallets_path, std::string()),
    (u32, log_level, 0),
    (u32, log_trunc_to_size, 0),
    (bool, log_console, false),
    (std::string, log_filename, std::string()),
    (std::string, log_categories, std::string())
);

GRAFT_DEFINE_IO_STRUCT_INITED(Running,
    (u64, http_request_total, 0),
    (u64, http_request_routed, 0),
    (u64, http_request_unrouted, 0),

    (u64, http_resp_status_ok, 0),
    (u64, http_resp_status_error, 0),
    (u64, http_resp_status_drop, 0),
    (u64, http_resp_status_busy, 0),

    (u64, http_req_bytes_raw, 0),
    (u64, http_resp_bytes_raw, 0),

    (u64, upstrm_http_req, 0),
    (u64, upstrm_http_resp_ok, 0),
    (u64, upstrm_http_resp_err, 0),

    (u64, upstrm_http_req_bytes_raw, 0),
    (u64, upstrm_http_resp_bytes_raw, 0),

    (u32, uptime_sec, 0)
);

GRAFT_DEFINE_IO_STRUCT_INITED(EndPoint,
    (std::string, path, std::string()),
    (std::string, handler, std::string()),
    (std::string, info, std::string())
);

GRAFT_DEFINE_IO_STRUCT_INITED(DapiEntry,
    (std::string, protocol, std::string()),
    (std::string, version, std::string()),
    (std::vector<EndPoint>, end_points, std::vector<EndPoint>())
);

GRAFT_DEFINE_IO_STRUCT_INITED(Request,
    (int, request, 0)
);

GRAFT_DEFINE_IO_STRUCT_INITED(Response,
    (std::string, version, std::string()),
    (Configuratioon, configuration, Configuratioon()),
    (Running, running_info, Running()),
    (std::vector<DapiEntry>, dapi, std::vector<DapiEntry>()),
    (std::vector<GraftletInfo>, graftlets, std::vector<GraftletInfo>{})
);

GRAFT_DEFINE_JSON_RPC_REQUEST(ReqJsonRpc, Request)
GRAFT_DEFINE_JSON_RPC_RESPONSE_RESULT(RespJsonRpc, Response);

void register_request(Router& router);

}

