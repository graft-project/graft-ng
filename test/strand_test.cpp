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

#include "lib/graft/thread_pool/strand.hpp"
#include <gtest/gtest.h>
#include <atomic>
#include <string>
#include <functional>

using namespace std;
using namespace tp;

namespace
{

void taskHandler(int value, std::atomic<int>& current_value)
{
  EXPECT_GT(value, current_value.load(std::memory_order_acquire));

  current_value.store(value, std::memory_order_release);

  std::this_thread::sleep_for(std::chrono::milliseconds(250));
}

}

TEST(StrandTest, basic)
{
    ThreadPool thread_pool;
    Strand strand(thread_pool, 16);

    std::atomic<int> value = 0;

    const int max_value = 7;

    for (int i=0; i<max_value; i++)
        strand.post(std::bind(&taskHandler, i+1, std::ref(value)));

    while (value.load(std::memory_order_acquire) < max_value)
        std::this_thread::sleep_for(std::chrono::milliseconds(250));

    EXPECT_EQ(value.load(std::memory_order_acquire), max_value);
}
