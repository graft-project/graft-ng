#pragma once

#include <string>
#include <vector>

#include "jsonrpc.h"

//#include "router.h"
//using Input = InHttp;
//using Output = OutHttp;
namespace graft { template<typename In, typename Out> class RouterT; class InHttp; class OutHttp; using Router = RouterT<InHttp, OutHttp>; }

namespace graft { namespace supernode { namespace request { namespace system_info {

GRAFT_DEFINE_IO_STRUCT_INITED(Configuratioon,
    (std::string, some_value, std::string())
);

GRAFT_DEFINE_IO_STRUCT_INITED(Running,
    (std::string, some_value, std::string())
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

