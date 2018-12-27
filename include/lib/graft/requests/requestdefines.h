
#pragma once

#include "lib/graft/graft_constants.h"
#include "lib/graft/inout.h"
#include "lib/graft/router.h"
#include "rta/supernode.h"

#include <chrono>

GRAFT_DEFINE_IO_STRUCT(ErrorResponse,
    (int64, code),
    (std::string, message)
);

#define ERROR_MESSAGE(message) std::string(__FUNCTION__) + std::string(": ") + message
#define EXTENDED_ERROR_MESSAGE(message, reason) \
    std::string(__FUNCTION__) + std::string(": ") + message + std::string(" Reason: ") + reason

#define STATUS_OK                           0

//Standart JSON-RPC 2.0 Errors
#define ERROR_PARSE_ERROR                   -32700
#define ERROR_INVALID_REQUEST               -32600
#define ERROR_METHOD_NOT_FOUND              -32601
#define ERROR_INVALID_PARAMS                -32602
#define ERROR_INTERNAL_ERROR                -32603

static const std::string MESSAGE_INVALID_PARAMS("The request parameters are invalid.");
static const std::string MESSAGE_INTERNAL_ERROR("Internal server error.");

namespace graft {

Status errorInvalidParams(Output &output);

} //namespace graft

