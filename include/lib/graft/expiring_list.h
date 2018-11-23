
#pragma once

#include "lib/graft/context.h"

#include <deque>

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
//        m_cont.emplace_back(std::make_pair(now + m_delta, Uuid(uuid)));
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
    ExpiringListT(int life_time_ms) : m_delta( std::chrono::milliseconds(life_time_ms) ) { }
};

}
