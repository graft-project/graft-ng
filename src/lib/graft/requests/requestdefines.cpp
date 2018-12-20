
#include "lib/graft/requests/requestdefines.h"
#include "lib/graft/jsonrpc.h"

namespace graft {

Status errorInvalidParams(Output& output)
{
    JsonRpcError err;
    err.code = ERROR_INVALID_PARAMS;
    err.message = MESSAGE_INVALID_PARAMS;
    JsonRpcErrorResponse resp;
    resp.error = err;
    output.load(resp);
    return Status::Error;
}

}
