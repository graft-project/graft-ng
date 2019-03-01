
#pragma once

#include "lib/graft/context.h"

#include <deque>
#include <chrono>
#include <unordered_map>
#include <string>

namespace graft::detail {

template<typename Uuid = Context::uuid_t, typename Clock = std::chrono::steady_clock>
class ExpiringListT
{
    using Pair = std::pair<typename Clock::time_point, Uuid>;

    typename Clock::duration m_delta;

    std::deque<Pair> m_cont;
    void chop(const typename Clock::time_point& now = Clock::now())
    {
        if(m_cont.empty() || now < m_cont.begin()->first) return;
        auto it = std::lower_bound(++m_cont.begin(), m_cont.end(), std::make_pair(now, Uuid()),
                                   [](const auto& l, const auto& r)->bool { return l.first < r.first; });
        m_cont.erase(m_cont.begin(), it);
    }
public:
    void add(const Uuid& uuid)
    {
        typename Clock::time_point now = Clock::now();
        chop(now);
        m_cont.emplace_back(std::make_pair(now + m_delta, uuid));
    }
    bool remove(const Uuid& uuid)
    {
        return extract(uuid).first;
    }
    std::pair<bool,Uuid> extract(const Uuid& uuid)
    {
        if(m_cont.empty()) return std::make_pair(false, Uuid());
        chop();
        auto it = std::find_if(m_cont.begin(), m_cont.end(), [&uuid](const auto& v)->bool { return v.second == uuid; } );
        if(it == m_cont.end()) return std::make_pair(false, Uuid());
        auto res = std::make_pair(true, it->second);
        m_cont.erase(it);
        return res;
    }
    size_t count() const { return m_cont.size(); }
    ExpiringListT(int life_time_ms) : m_delta( std::chrono::milliseconds(life_time_ms) ) { }
};

class ExpiringSet
{
protected:
    using Clock = std::chrono::steady_clock;

    Clock::duration m_expire_ms;
    std::unordered_map<std::string,int> m_map;
    Clock::time_point m_last_rehash;
    size_t m_last_size;

    using MapIter = typename decltype(m_map)::iterator;

//    std::deque<std::tuple<Clock::time_point, MapIter, const std::string&>> m_deque;
    std::deque<std::tuple<Clock::time_point, MapIter, std::string>> m_deque;

    void chop(Clock::time_point& now)
    {
        Clock::time_point now_exp = now - m_expire_ms;
        while(!m_deque.empty())
        {
            auto& item = m_deque.front();
            Clock::time_point& tp = std::get<0>(item);
            if(now_exp < tp) break;
            MapIter it = (tp < m_last_rehash)? m_map.find(std::get<2>(item)) : std::get<1>(item);
            assert(it != m_map.end());
            if(--it->second == 0)
                m_map.erase(it);
            m_deque.pop_front();
        }
    }
public:
    ExpiringSet(uint expire_ms)
        : m_expire_ms(std::chrono::milliseconds(expire_ms))
        , m_last_rehash(Clock::now())
        , m_last_size(m_map.max_load_factor() * m_map.bucket_count())
    { }

    bool emplace(const std::string& s)
    {
        Clock::time_point now = Clock::now();
        chop(now);

        auto res = m_map.emplace(s, 1);
        if(res.second)
        {
            if(m_last_size < (m_map.max_load_factor() * m_map.bucket_count()))
            {//iterators invalidated
                m_last_rehash = now;
            }
        }
        else
        {
            ++res.first->second;
        }
        m_deque.emplace_back(std::make_tuple(now, res.first, res.first->first));
        return res.second;
    }

    size_t count() const
    {
        size_t cnt = 0;
        for(auto& it : m_map)
        {
            cnt += it.second;
        }
        assert(cnt == m_deque.size());
        return m_deque.size();
    }

    size_t unique_count() const
    {
        return m_map.size();
    }
};

} //namespace graft::detail
