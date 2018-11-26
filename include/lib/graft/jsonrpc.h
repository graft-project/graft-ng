// Copyright (c) 2018, The Graft Project
//
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without modification, are
// permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice, this list of
//    conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright notice, this list
//    of conditions and the following disclaimer in the documentation and/or other
//    materials provided with the distribution.
//
// 3. Neither the name of the copyright holder nor the names of its contributors may be
//    used to endorse or promote products derived from this software without specific
//    prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
// EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
// THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
// STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
// THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.


#pragma once

#include "lib/graft/inout.h"

#include <string>
#include <vector>

namespace graft {


/*!
 * \brief   defines new stucture for json-rpc request
 * \param   Name - name of new type to be defined
 * \param   Param - name of the existing type of parameter to be used in "params" array.
 *          Type must be defined with GRAFT_DEFINE_IO_STRUCT
 */
#define GRAFT_DEFINE_JSON_RPC_REQUEST(Name, Param)              \
    GRAFT_DEFINE_IO_STRUCT_INITED(Name,                         \
        (std::string,         jsonrpc,     "2.0"),              \
        (std::string,         method,   ""),                    \
        (uint64_t,            id,       0),                     \
        (Param,               params,  Param())                 \
    );

/*!
 *  JsonRpcRequestHeader - Helper structure to parse JSON-RPC request and get method/id fiends
 */
GRAFT_DEFINE_IO_STRUCT_INITED(JsonRpcRequestHeader,    \
     (std::string,         jsonrpc,     "2.0"),        \
     (std::string,         method,   ""),              \
     (uint64_t,            id,       0)                \
);


/*!
 * \brief initJsonRpcRequest - initializes json-rpc request with id, method name and params
 * \param t - instance of the T type (Request type previously defined with GRAFT_DEFINE_JSON_RPC_REQUEST)
 * \param id - id for json-rpc
 * \param method - method name
 * \param params - vector of params
 */
template <typename T, typename P>
void initJsonRpcRequest(T &t, uint64_t id, const std::string &method, const P &params)
{
    t.id = id;
    t.method = method;
    t.jsonrpc = "2.0";
    t.params = std::move(params);
}
GRAFT_DEFINE_IO_STRUCT_INITED(JsonRpcError,
                       (int64_t, code, 0),
                       (std::string, message, "")
                       );

/*!
 * \brief   defines new stucture for json-rpc response containing both error and result fields
 * \param   Name - name of new type to be defined
 * \param   Result - name of the existing type of parameter to be used in "result" field in the json-rpc response.
 *          Type must be defined with GRAFT_DEFINE_IO_STRUCT
 */
#define GRAFT_DEFINE_JSON_RPC_RESPONSE(Name, Result) \
    GRAFT_DEFINE_IO_STRUCT_INITED(Name,                    \
        (std::string,         jsonrpc, "2.0"),             \
        (uint64_t,            id,          0),             \
        (Result,              result,  Result()),          \
        (JsonRpcError,        error,   JsonRpcError())     \
    );

/*!
 * \brief  defines new stucture for json-rpc response containing only result field
 * \param  Name - name of new type to be defined
 * \param  Result - name of the existing type of parameter to be used in "result" field in the json-rpc response.
 *          Type must be defined with GRAFT_DEFINE_IO_STRUCT
 */
#define GRAFT_DEFINE_JSON_RPC_RESPONSE_RESULT(Name, Result) \
    GRAFT_DEFINE_IO_STRUCT_INITED(Name,               \
        (std::string,         jsonrpc,  "2.0"),       \
        (uint64_t,            id,          0),        \
        (Result,              result,      Result())  \
    );


GRAFT_DEFINE_IO_STRUCT_INITED(JsonRpcErrorResponse,      \
        (std::string,         jsonrpc, "2.0"),           \
        (uint64_t,            id,         0 ),           \
        (JsonRpcError,        error,   JsonRpcError())   \
    );
}

