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

#include <gtest/gtest.h>

#include "lib/graft/binary_serialize.h"

#include "include_base_utils.h"
using namespace epee;

#include "cryptonote_basic/cryptonote_format_utils.h"
#include "serialization/binary_utils.h"

#include "lib/graft/jsonrpc.h"

namespace
{
GRAFT_DEFINE_IO_STRUCT(Payment,
      (uint64, amount),
      (uint32, block_height),
      (std::string, payment_id),
      (std::string, tx_hash),
      (uint32, unlock_time)
);

GRAFT_DEFINE_IO_STRUCT(NestedPayment,
      (std::string, s),
      (Payment, p),
      (uint32, i)
);

GRAFT_DEFINE_IO_STRUCT(WithVector,
      (std::vector<std::string>, vs),
      (std::vector<Payment>, vp)
);

//TODO: does not work
GRAFT_DEFINE_IO_STRUCT(WithVectorInt,
      (std::vector<uint32>, v)
);

struct PaymentX
{
  uint64 amount;
  uint32 block_height;
  std::string payment_id;
  std::string tx_hash;
  uint32 unlock_time;
  BEGIN_SERIALIZE()
    FIELD(amount)
    FIELD(block_height)
    FIELD(payment_id)
    FIELD(tx_hash)
    FIELD(unlock_time)
  END_SERIALIZE()
};

struct NestedPaymentX
{
  std::string s;
  PaymentX p;
  uint32 i;
  BEGIN_SERIALIZE()
    FIELD(s)
    FIELD(p)
    FIELD(i)
  END_SERIALIZE()
};

struct WithVectorX
{
  std::vector<std::string> vs;
  std::vector<PaymentX> vp;
  BEGIN_SERIALIZE()
    FIELD(vs)
    FIELD(vp)
  END_SERIALIZE()
};

//TODO: does not work
struct WithVectorIntX
{
  std::vector<uint32> v;
  BEGIN_SERIALIZE()
    FIELD(v)
  END_SERIALIZE()
};

} //namespace

namespace graft { namespace serializer {

template<typename T>
struct Nothing
{
    static std::string serialize(const T& t)
    {
        return "";
    }
    static void deserialize(const std::string& s, T& t)
    {
    }
};

} } //namespace graft { namespace serializer

TEST(InOut, serialization)
{
    using namespace graft;
    namespace serial = graft::serializer;

    GRAFT_DEFINE_IO_STRUCT(J,
        (int,x),
        (int,y)
    );

    J j;

    Input input;
    input.body = "{\"x\":1,\"y\":2}";
        j.x = 5; j.y = 6;
    j = input.get<J, serial::JSON<J>>();
    j = input.get<J>();
        EXPECT_EQ(j.x, 1); EXPECT_EQ(j.y, 2);
    j = input.get<J>();
        EXPECT_EQ(j.x, 1); EXPECT_EQ(j.y, 2);
        j.x = 5; j.y = 6;
    j = input.getT<serial::JSON, J>();
//    j = input.getT<J>();
        EXPECT_EQ(j.x, 1); EXPECT_EQ(j.y, 2);

    Output output;
    output.load<J, serial::JSON<J>>(j);
    output.load<J>(j);
    output.load<>(j);
        EXPECT_EQ(input.body, output.body);
        output.body.clear();
    output.load(j);
        EXPECT_EQ(input.body, output.body);

    output.body.clear();
    output.loadT<serial::JSON, J>(j);
    output.loadT<serial::JSON>(j);
    output.loadT<>(j);
        EXPECT_EQ(input.body, output.body);
        output.body.clear();
    output.loadT(j);
        EXPECT_EQ(input.body, output.body);

    struct A
    {
        int x;
        int y;
    };

    A a;

    a = input.get<A, serial::Nothing<A>>();
    a = input.getT<serial::Nothing, A>();
    output.load<A, serial::Nothing<A>>(a);
    output.loadT<serial::Nothing, A>(a);
    output.loadT<serial::Nothing>(a);

    {
        output.load<J, serial::Binary<J>>(j);
        std::string s = output.body;
        input.body = s;
        J j1;
        j1 = input.getT<serial::Binary, J>();
        EXPECT_EQ(j.x, j1.x);
        EXPECT_EQ(j.y, j1.y);
    }

    Payment p{ {}, 1, 2, "abc", "defg", 5 };
    output.load<Payment, serial::Binary<Payment>>(p);
    PaymentX px{ 1, 2, "abc", "defg", 5 };
    std::string blob;
    ::serialization::dump_binary(px, blob);
    EXPECT_EQ(output.body, blob);

    Payment p1;
    input.body = output.body;
    p1 = input.get<Payment, serial::Binary<Payment>>();
    EXPECT_EQ(p.amount, p1.amount);
    EXPECT_EQ(p.block_height, p1.block_height);
    EXPECT_EQ(p.payment_id, p1.payment_id);
    EXPECT_EQ(p.tx_hash, p1.tx_hash);
    EXPECT_EQ(p.unlock_time, p1.unlock_time);

    {
        NestedPayment np{ {}, "something", p, 123 };
        output.load<NestedPayment, serial::Binary<NestedPayment>>(np);
        NestedPaymentX npx{ "something", px, 123 };
    //    blob.clear();
        ::serialization::dump_binary(npx, blob);
        EXPECT_EQ(output.body, blob);

        NestedPayment np1;
        input.body = output.body;
        np1 = input.get<NestedPayment, graft::serializer::Binary<NestedPayment>>();
        EXPECT_EQ(np.s, np1.s);
        EXPECT_EQ(np.p.amount, np1.p.amount);
        EXPECT_EQ(np.p.block_height, np1.p.block_height);
        EXPECT_EQ(np.p.payment_id, np1.p.payment_id);
        EXPECT_EQ(np.p.tx_hash, np1.p.tx_hash);
        EXPECT_EQ(np.p.unlock_time, np1.p.unlock_time);
        EXPECT_EQ(np.i, np1.i);
    }
    {
        WithVector wv{ {}, {"1","2","3","4","5"}, {p, p, p} };
        output.load<WithVector, graft::serializer::Binary<WithVector>>(wv);
        WithVectorX wvx{ {"1","2","3","4","5"}, {px, px, px} };
        ::serialization::dump_binary(wvx, blob);
        EXPECT_EQ(output.body, blob);

        input.body = output.body;
        WithVector wv1 = input.get<WithVector, graft::serializer::Binary<WithVector>>();
        EXPECT_EQ(wv.vs, wv1.vs);
        EXPECT_EQ(wv.vp.size(), wv1.vp.size());
        for(size_t i = 0; i<wv.vp.size(); ++i)
        {
            auto& p = wv.vp[i];
            auto& p1 = wv1.vp[i];
            EXPECT_EQ(p.amount, p1.amount);
            EXPECT_EQ(p.block_height, p1.block_height);
            EXPECT_EQ(p.payment_id, p1.payment_id);
            EXPECT_EQ(p.tx_hash, p1.tx_hash);
            EXPECT_EQ(p.unlock_time, p1.unlock_time);
        }
    }
//does not work yet
#if 0
    {
        WithVectorInt wv{ {}, {1,2,3,4,5} };
        output.load<WithVectorInt, graft::serializer::Binary<WithVectorInt>>(wv);
        WithVectorIntX wvx{ {1,2,3,4,5} };
        ::serialization::dump_binary(wvx, blob);
        EXPECT_EQ(output.body, blob);

        input.body = output.body;
        WithVectorInt wv1 = input.get<WithVectorInt, graft::serializer::Binary<WithVectorInt>>();
        EXPECT_EQ(wv.v, wv1.v);
    }
#endif
}

