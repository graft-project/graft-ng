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

#include <string>
#include <gtest/gtest.h>
#include <inout.h>
#include <jsonrpc.h>

GRAFT_DEFINE_IO_STRUCT(Payment,
     (uint64, amount),
     (uint32, block_height),
     (std::string, payment_id),
     (std::string, tx_hash),
     (uint32, unlock_time)
 );




TEST(JsonRPCFormat, common)
{
    using namespace graft;
    Payment p;
    p.amount = 1;
    p.block_height = 1;
    p.payment_id = "123";
    std::vector<Payment> params = {p};

    GRAFT_DEFINE_JSON_RPC_REQUEST(JsonRPCRequest, Payment);
    JsonRPCRequest jreq;
    initJsonRpcRequest(jreq, 1, "hello", params);
    std::cout << jreq.toJson().GetString() << std::endl;

    GRAFT_DEFINE_JSON_RPC_RESPONSE(JsonRPCResponse, Payment);

    std::string json_rpc_error     = " {\"json\":\"\",\"id\":3355185,\"error\":{\"code\":123,\"message\":\"Error Message\"}}";
    std::string json_rpc_response  = " {\"json\":\"\",\"id\":3355185,\"result\":{\"amount\":0,\"block_height\":3581286912,\"payment_id\":\"\",\"tx_hash\":\"\",\"unlock_time\":1217885840}}";
    // JsonRPCResponse jresp;
    // std::cout << jresp.toJson().GetString() << std::endl;
    Input in_err; in_err.load(json_rpc_error);
    Input in_result; in_result.load(json_rpc_response);

    JsonRPCResponse response;
    ASSERT_NO_THROW(response = in_result.get<JsonRPCResponse>());
    GRAFT_DEFINE_JSON_RPC_RESPONSE_RESULT(JsonRPCResponseResult, Payment);

    // we have positive response (no error);

    if (response.error.code == 0) {
        JsonRPCResponseResult result = in_result.get<JsonRPCResponseResult>();
        // alternatively, we can just copy member-by-member from result
        // Use result;
    } else {
        JsonRpcErrorResponse errorResponse = in_result.get<JsonRpcErrorResponse>();
        // Use error;
    }

}


