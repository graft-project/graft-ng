#ifndef JSONRPC_H
#define JSONRPC_H

#include <inout.h>

namespace graft {

#define GRAFT_DEFINE_JSON_RPC_REQUEST(Name, Param) \
    GRAFT_DEFINE_IO_STRUCT(Name,          \
        (std::string,         json),      \
        (std::string,         method),    \
        (uint64_t,            id),        \
        (std::vector<Param>,  params)     \
    );

template <typename T, typename P>
void initJsonRpcRequest(T &t, uint64_t id, const std::string &method, const std::vector<P> &params)
{
    t.id = id;
    t.method = method;
    t.json = "2.0";
    t.params = std::move(params);
}

GRAFT_DEFINE_IO_STRUCT(JsonRpcError,
                       (int64_t, code),
                       (std::string, message)
                       );


#define GRAFT_DEFINE_JSON_RPC_RESPONSE(Name, Result) \
    GRAFT_DEFINE_IO_STRUCT(Name,          \
        (std::string,         json),      \
        (uint64_t,            id),        \
        (Result,              result),    \
        (JsonRpcError,        error)      \
    );

#define GRAFT_DEFINE_JSON_RPC_RESPONSE_RESULT(Name, Result) \
    GRAFT_DEFINE_IO_STRUCT(Name,          \
        (std::string,         json),      \
        (uint64_t,            id),        \
        (Result,              result),    \
    );


#define GRAFT_DEFINE_JSON_RPC_RESPONSE_ERROR(Name) \
    GRAFT_DEFINE_IO_STRUCT(Name,          \
        (std::string,         json),      \
        (uint64_t,            id),        \
        (JsonRpcError,        error),     \
    );

}

#endif // JSONRPC_H
