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
#include "lib/graft/ConfigIni.h"
#include "lib/graft/graft_exception.h"

TEST(ConfigIni, common)
{
    graft::ConfigIniSubtree ci = graft::ConfigIniSubtree::create("./config.ini");
    {
        int i = ci.get<int>("server.lru-timeout-ms");
        std::optional<int> oi = ci.get_optional<int>("server.lru-timeout-ms");
        EXPECT_EQ(oi.has_value(), true);
        EXPECT_EQ(oi.value(), i);
        int i1 = ci.get<int>("server.bla-bla", 200);
        EXPECT_EQ(i1, 200);
        std::optional<int> oi1 = ci.get_optional<int>("server.bla-bla");
        EXPECT_EQ(oi1.has_value(), false);
        bool except = false;
        try
        {
            int i = ci.get<int>("server.bla-bla");
        }
        catch(graft::exit_error& e)
        {
            except = true;
        }
        EXPECT_EQ(except, true);
    }
    {
        EXPECT_NO_THROW( ci.get<bool>("stun.enabled") );
        std::optional<bool> ob = ci.get_optional<bool>("stun.enabled");
        EXPECT_EQ(ob.has_value(), true);
    }
    {
        //check comments trim
        std::string s = ci.get<std::string>("server.workers-expelling-interval-ms");
        EXPECT_LE(s.size(), std::string("20000").size());
        std::optional<std::string> os = ci.get_optional<std::string>("server.workers-expelling-interval-ms");
        EXPECT_EQ(os.has_value(), true);
        EXPECT_EQ(os.value(), s);
        std::string s1 = ci.get<std::string>("server.bla-bla", "default");
        EXPECT_EQ(s1, "default");
        std::optional<std::string> os1 = ci.get_optional<std::string>("server.bla-bla");
        EXPECT_EQ(os1.has_value(), false);
        bool except = false;
        try
        {
            std::string s1 = ci.get<std::string>("server.bla-bla");
        }
        catch(graft::exit_error& e)
        {
            except = true;
        }
        EXPECT_EQ(except, true);
    }

    graft::ConfigIniSubtree uri_subst_conf = ci.get_child("upstream");

    {
        auto opt = ci.get_child_optional("asdf");
        EXPECT_EQ(bool(opt), false);
        auto opt1 = ci.get_child_optional("upstream");
        EXPECT_EQ(bool(opt1), true);
        uri_subst_conf = std::move(opt1.value());
    }

    int cnt1 = 0;
    uri_subst_conf.begin();
    graft::ConfigIniSubtreeRef ref = *uri_subst_conf.begin();
    std::for_each(uri_subst_conf.begin(), uri_subst_conf.end(),[&uri_subst_conf, &cnt1](auto it)
    {
        ++cnt1;
        std::string name(it.name());
        std::string val(uri_subst_conf.get<std::string>(name));
        EXPECT_EQ(val.empty(), false);
        graft::ConfigIniSubtree v = it.value();
        std::string val1 = v.get<std::string>("");
        graft::ConfigIniSubtreeRef ref = it;
        std::string val2 = ref.value().get<std::string>("");
        EXPECT_EQ(val, val1);
    });
    EXPECT_LT(0, cnt1);

    int cnt2 = 0;
    for(graft::ConfigIniSubtree::iterator it = uri_subst_conf.begin(), eit = uri_subst_conf.end(); it != eit; ++it)
    {
        ++cnt2;
        std::string name = it->name();
        std::string val = it->value().get<std::string>("");
        std::string val1 = uri_subst_conf.get<std::string>(name);
        EXPECT_EQ(val, val1);
    }
    EXPECT_LT(0, cnt2);
}
