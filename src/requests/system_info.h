#pragma once

#include <string>
#include <vector>
#include <cstdint>

#include "jsonrpc.h"

//#include "router.h"
//using Input = InHttp;
//using Output = OutHttp;


/*
Plan part A
systemUptimeSec

systemHttpRespStatusOK                    (200)
systemHttpRespStatusError                 (500)
systemHttpRespStatusDrop                  (400)
systemHttpRespStatusBusy                  (503)

systemUpstreamHttpReq
systemUpstreamHttpResp


systemHttpReqBytesRow
systemHttpRespBytesRow

systemUpstreamHttpReqBytesRow
systemUpstreamHttpRespBytesRow
*/

namespace graft { template<typename In, typename Out> class RouterT; class InHttp; class OutHttp; using Router = RouterT<InHttp, OutHttp>; }

namespace graft { namespace supernode { namespace request { namespace system_info {

using u32 = std::uint32_t;
using u64 = std::uint64_t;

GRAFT_DEFINE_IO_STRUCT_INITED(Configuratioon,
    (std::string, some_value, std::string())
);

GRAFT_DEFINE_IO_STRUCT_INITED(Running,
    (std::string, some_value, std::string()),
    (u64, http_request_total_cnt, 0),
    (u64, http_request_routed_cnt, 0),
    (u64, http_request_unrouted_cnt, 0),
    (u64, system_http_resp_status_ok, 0),
    (u64, system_http_resp_status_error, 0),
    (u64, system_http_resp_status_drop, 0),
    (u64, system_http_resp_status_busy, 0),
    (u64, system_upstrm_http_req_cnt, 0),
    (u64, system_upstrm_http_resp_cnt, 0),
    (u32, system_uptime_sec, 0)
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

GRAFT_DEFINE_IO_STRUCT_INITED(GraftletDependency,
    (std::string, name, std::string()),
    (std::string, min_version, std::string())
);

GRAFT_DEFINE_IO_STRUCT_INITED(Graftlet,
    (std::string, name, std::string()),
    (std::string, version, std::string()),
    (std::string, status, std::string()),
    (std::vector<EndPoint>, end_points, std::vector<EndPoint>()),
    (std::vector<GraftletDependency>, requires, std::vector<GraftletDependency>())
);

GRAFT_DEFINE_IO_STRUCT_INITED(Request,
    (int, request, 0)
);

GRAFT_DEFINE_IO_STRUCT_INITED(Response,
    (std::string, version, std::string()),
    (Configuratioon, configuration, Configuratioon()),
    (Running, runningInfo, Running()),
    (std::vector<DapiEntry>, dapi, std::vector<DapiEntry>()),
    (std::vector<Graftlet>, graftlets, std::vector<Graftlet>())
);

GRAFT_DEFINE_JSON_RPC_REQUEST(ReqJsonRpc, Request)
GRAFT_DEFINE_JSON_RPC_RESPONSE_RESULT(RespJsonRpc, Response);

void register_request(Router& router);

} } } }

