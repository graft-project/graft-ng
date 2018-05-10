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

#include "inout.h"
#include "jsonrpc.h"
#include <gtest/gtest.h>
#include <string>


using namespace std;
using namespace graft;

GRAFT_DEFINE_IO_STRUCT(PaymentTest,
     (uint64, amount),
     (uint32, block_height),
     (std::string, payment_id),
     (std::string, tx_hash),
     (uint32, unlock_time)
 );


TEST(JsonParse, common)
{
    GRAFT_DEFINE_JSON_RPC_RESPONSE(JsonRPCResponseTest, PaymentTest);

    std::string json_valid  = "{\"json\":\"\",\"id\":3355185,\"result\":{\"amount\":0, \"block_height\":3581286912,\"payment_id\":\"\",\"tx_hash\":\"\",\"unlock_time\":1217885840}}";
    std::string json_invalid  = "{\"json\":\"\",\"id\":3355185,\"result\":{\"amount\":0 'aaaa' = 'bbb',\"block_height\":3581286912,\"payment_id\":\"\",\"tx_hash\":\"\",\"unlock_time\":1217885840}}";

    // valid json test
    Input in; in.load(json_valid);
    JsonRPCResponseTest result1;


    ASSERT_NO_THROW(result1 = in.get<JsonRPCResponseTest>());
    ASSERT_EQ(result1.id, 3355185);
    ASSERT_EQ(result1.result.block_height, 3581286912);



    JsonRPCResponseTest result2;
    bool ret = in.get<JsonRPCResponseTest>(result2);




    ASSERT_EQ(result2.id, 3355185);
    ASSERT_EQ(result2.result.block_height, 3581286912);

    // invalid json test
    in.load(json_invalid);

    ASSERT_ANY_THROW(result1 = in.get<JsonRPCResponseTest>());

    ASSERT_FALSE(in.get<JsonRPCResponseTest>(result1));


}

