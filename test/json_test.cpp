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

#include "lib/graft/inout.h"
#include "lib/graft/jsonrpc.h"

#include <gtest/gtest.h>
#include <string>

using namespace std;
using namespace graft;

struct Foo
{
    int foo = 1;
};

// conflicting Payment struct defined with GRAFT_DEFINE_IO_STRUCT in another file (graft_server_test.cpp)
//
GRAFT_DEFINE_IO_STRUCT_INITED(Payment,
     (uint64, amount, 1),
     (uint32, block_height, 2),
     (std::string, payment_id, "123"),
     (std::string, tx_hash, "456"),
     (uint32, unlock_time, 3)
 );

GRAFT_DEFINE_IO_STRUCT_INITED(Payment3,
     (uint64, amount, 1),
     (uint32, block_height, 2),
     (std::string, payment_id, "123"),
     (std::string, tx_hash, "456"),
     (uint32, unlock_time, 3)
 );


TEST(GraftDefineIOStruct, initedSuccess)
{
    Payment3 pt3;
    EXPECT_EQ(pt3.amount, 1);
    EXPECT_EQ(pt3.block_height, 2);
    EXPECT_EQ(pt3.payment_id, "123");
    EXPECT_EQ(pt3.tx_hash, "456");
    EXPECT_EQ(pt3.unlock_time, 3);
}

TEST(JsonParseTest, malformed_source)
{
    std::string json_rpc_error     = " {\"json\":\"2.0;123,,,\",+,\"id\":3355185,\"error\":{\"code\":123,\"message\":\"Error Message\"}}";
    Input in; in.load(json_rpc_error);
    JsonRpcErrorResponse resp;
    EXPECT_ANY_THROW(in.get<JsonRpcErrorResponse>());
    EXPECT_FALSE(in.get(resp));
}

