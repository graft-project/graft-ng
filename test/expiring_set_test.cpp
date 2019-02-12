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
#include <chrono>
#include <thread>
#include <unordered_set>
#include "lib/graft/expiring_list.h"
#include "crypto/crypto.h"
#include "string_tools.h"

namespace graft { namespace detail {

template<typename T>
struct Str2T
{
    T operator () (const std::string& s) const
    {
        T res;
        epee::string_tools::hex_to_pod(s, res);
        return res;
    }
};

template<>
struct Str2T<std::string>
{
    const std::string& operator () (const std::string& s) const { return s; }
};

template<typename T, template<typename> class Set = std::unordered_set, template<typename> class Convert = Str2T>
class ExpiringSetT
{
    using Clock = std::chrono::steady_clock;

    Set<T> m_set;
    Convert<T> m_convert;
    std::deque<std::pair<Clock::time_point, T>> m_deque;
    Clock::duration m_expire_ms;
    void chop(Clock::time_point now)
    {
        while(!m_deque.empty())
        {
            auto& item = m_deque.front();
            if(now < item.first) break;
            m_set.erase(item.second);
            m_deque.pop_front();
        }
    }
public:
    ExpiringSetT(uint expire_ms) : m_expire_ms(std::chrono::milliseconds(expire_ms)) { }

    std::pair<typename Set<T>::iterator, bool> emplace(const std::string& s)
    {
        using TT = decltype(m_convert(s)); //can be T or T&
        TT t = m_convert(s);
        auto res = m_set.emplace(t);
        if(!res.second) return res;
        Clock::time_point now = Clock::now();
        chop(now);
        m_deque.emplace_back(std::make_pair(now + m_expire_ms, t));
        return res;
    }

    //the result of emplace(...).first can be used
    void erase(typename Set<T>::iterator it)
    {
        m_set.erase(it);
    }

    size_t count() const { return m_set.size(); }
};

} } //namespace graft { namespace detail {

namespace std
{
//it is required for graft::detail::ExpiringSetT<crypto::public_key, std::set>, as it has std::set<crypto::public_key>
template<>
struct less<crypto::public_key> : public binary_function<crypto::public_key, crypto::public_key, bool>
{
    bool
    operator()(const crypto::public_key& f, const crypto::public_key& s) const
    {
        const signed char* pf = (const signed char*)&f;
        const signed char* ps = (const signed char*)&s;
        for(int i=sizeof(f); 0<i; --i, ++pf, ++ps)
        {
            signed char d = *ps - *pf;
            if(!d) continue;
            if(d < 0) return true;
            return false;
        }
        return false;
    }
};
} //namespace std

TEST(ExpiringSet, common)
{
    const size_t sampleCount = 10000;

    std::vector<std::string> hex_vec;
    {
        std::vector<std::string> vals;
        std::vector<crypto::public_key> pk_vec;
        for(int i = 0; i<sampleCount; ++i)
        {
            crypto::public_key pk;
            crypto::secret_key sk;
            crypto::generate_keys(pk, sk);
            pk_vec.emplace_back(pk);
        }
        for(auto& v : pk_vec)
        {
            hex_vec.emplace_back(epee::string_tools::pod_to_hex(v));
        }
    }

    int cnt_true, cnt_false, cnt_all;
    auto f = [&](auto& es)->void
    {
        cnt_true = 0, cnt_false = 0, cnt_all = 0;
        for(int cnt = sampleCount; cnt; cnt/=2)
        {
            cnt_all += cnt;
            for(int i = 0; i < cnt; ++i)
            {
                bool res = es.emplace(hex_vec[i]);
                if(res) ++cnt_true;
                else ++cnt_false;
            }
        }
    };

    {
        const int ms = 4;
        graft::detail::ExpiringSet es(ms);
        f(es);
        EXPECT_EQ(cnt_all, cnt_true + cnt_false);
        EXPECT_TRUE(hex_vec.size() < cnt_true);
        EXPECT_TRUE(es.unique_count() < es.count());
        std::cout << "==> ExpiringSet(" << ms << ") : " << es.count() << "(" << es.unique_count() << ")\n";
    }
    {
        const int ms = 40000;
        graft::detail::ExpiringSet es(ms);
        f(es);
        EXPECT_EQ(cnt_all, cnt_true + cnt_false);
        EXPECT_EQ(cnt_true, hex_vec.size());
        EXPECT_TRUE(es.unique_count() < es.count());
        EXPECT_EQ(es.unique_count(), hex_vec.size());
        std::cout << "==> ExpiringSet(" << ms << ") : " << es.count() << "(" << es.unique_count() << ")\n";
    }
}

TEST(ExpiringSet, SetVsList)
{
    const size_t sampleCount = 10000;
    //const size_t sampleCount = 10;

    const int expire_ms = 4;
    using clock = std::chrono::steady_clock;

    std::vector<std::string> hex_vec;
    {
        std::vector<std::string> vals;
        std::vector<crypto::public_key> pk_vec;
        for(int i = 0; i<sampleCount; ++i)
        {
            crypto::public_key pk;
            crypto::secret_key sk;
            crypto::generate_keys(pk, sk);
            pk_vec.emplace_back(pk);
        }
        for(auto& v : pk_vec)
        {
            hex_vec.emplace_back(epee::string_tools::pod_to_hex(v));
        }
    }

    std::chrono::time_point tp0 = clock::now();

    graft::detail::ExpiringSetT<crypto::public_key, std::set> est_pod(expire_ms);
    int est_pod_false = 0;
    for(auto& s : hex_vec)
    {
        auto res = est_pod.emplace(s);
        if(!res.second) ++est_pod_false;
    }
    int est_pod_count = est_pod.count();

    std::chrono::time_point tp1 = clock::now();

    graft::detail::ExpiringSetT<std::string, std::set> est_str(expire_ms);
    int est_str_false = 0;
    for(auto& s : hex_vec)
    {
        auto res = est_str.emplace(s);
        if(!res.second) ++est_str_false;
    }
    int est_str_count = est_str.count();

    std::chrono::time_point tp2 = clock::now();

    graft::detail::ExpiringListT<crypto::public_key> elt_pod(expire_ms);
    int elt_pod_false = 0;
    for(auto& s : hex_vec)
    {
        crypto::public_key pk;
        epee::string_tools::hex_to_pod(s, pk);
        auto res = elt_pod.extract(pk);
        if(res.first) ++elt_pod_false;
        elt_pod.add(pk);
    }
    int elt_pod_count = elt_pod.count();

    std::chrono::time_point tp3 = clock::now();

    graft::detail::ExpiringListT<std::string> elt_str(expire_ms);
    int elt_str_false = 0;
    for(auto& s : hex_vec)
    {
        auto res = elt_str.extract(s);
        if(res.first) ++elt_str_false;
        elt_str.add(s);
    }
    int elt_str_count = elt_str.count();

    std::chrono::time_point tp4 = clock::now();

    graft::detail::ExpiringSet es(expire_ms);
    int cnt_es = 0;
    for(auto& s : hex_vec)
    {
        bool res = es.emplace(s);
        if(!res) ++cnt_es;
    }
    int es_count = es.count();

    std::chrono::time_point tp5 = clock::now();

    EXPECT_EQ(est_pod_false, 0);
    EXPECT_EQ(est_str_false, 0);
    EXPECT_EQ(elt_pod_false, 0);
    EXPECT_EQ(elt_str_false, 0);
    EXPECT_EQ(cnt_es, 0);

    EXPECT_LT(est_pod_count, sampleCount);
    EXPECT_LT(est_str_count, sampleCount);
    EXPECT_LT(elt_pod_count, sampleCount);
    EXPECT_LT(elt_str_count, sampleCount);
    EXPECT_LT(es_count, sampleCount);

    int mu1 = (std::chrono::duration_cast<std::chrono::microseconds>(tp1 - tp0)).count();
    int mu2 = (std::chrono::duration_cast<std::chrono::microseconds>(tp2 - tp1)).count();
    int mu3 = (std::chrono::duration_cast<std::chrono::microseconds>(tp3 - tp2)).count();
    int mu4 = (std::chrono::duration_cast<std::chrono::microseconds>(tp4 - tp3)).count();
    int mu5 = (std::chrono::duration_cast<std::chrono::microseconds>(tp5 - tp4)).count();
    std::cout << "\n==> ExpiringSetT: " << mu1 << "mu vs " << mu2 << "mu vs " << mu3 << "mu vs " << mu4 << "mu vs " << mu5 << "mu "
              << "sz1=" << est_pod_count << " sz2=" << est_str_count << " sz3=" << elt_pod_count << " sz3=" << elt_str_count
              << " sz4=" << es_count
              << "\n";
}
