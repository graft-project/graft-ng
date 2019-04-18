
#include "lib/graft/requests/requestdefines.h"
#include "lib/graft/jsonrpc.h"

namespace graft {

Status errorInvalidParams(Output& output)
{
    ErrorResponse err;
    err.code = ERROR_INVALID_PARAMS;
    err.message = MESSAGE_INVALID_PARAMS;
    output.load(err);
    return Status::Error;
}


}
