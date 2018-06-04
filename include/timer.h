#pragma once

#include <chrono>
#include <queue>
#include <functional>
#include <iostream>
#include <string>

namespace graft
{
    namespace ch = std::chrono;

    template<typename TR_ptr>
    class TimerList
    {
        struct timer
        {
            ch::milliseconds timeout;
            ch::milliseconds lap;
            TR_ptr ptr;

            timer(ch::milliseconds timeout, TR_ptr ptr)
                : timeout(timeout)
                , lap(ch::milliseconds::max())
                , ptr(ptr) {}

            timer() = delete;

            void start()
            {
                lap = ch::time_point_cast<ch::milliseconds>(
                    ch::steady_clock::now()
                ).time_since_epoch() + timeout;
            }

            bool fired()
            {
                return lap <=
                    ch::time_point_cast<ch::milliseconds>(
                        ch::steady_clock::now()
                    ).time_since_epoch();
            }
            void dump(const std::string &pref) const
            {
                std::cout << pref << " timeout: " << timeout.count()
                    << "; lap: " << lap.count() << std::endl;
            }

            bool operator < (const timer& other) const
            {
                return lap < other.lap;
            }
            bool operator > (const timer& other) const { return ! operator <(other); }
        };

        class priority_queue : public std::priority_queue<timer, std::vector<timer>, std::greater<timer>>
        {
            using base_t = std::priority_queue<timer, std::vector<timer>, std::greater<timer>>;
        public:
            const typename base_t::container_type& get_c() const { return base_t::c; }
        };

        priority_queue m_pq;

    public:

        void push(ch::milliseconds timeout, TR_ptr ptr)
        {
            timer t(timeout, ptr);
            t.start();
            m_pq.emplace(std::move(t));
        }

        void eval()
        {
            while(!m_pq.empty())
            {
                auto t = m_pq.top();
                if(!t.fired()) break;
                m_pq.pop();
                t.ptr->onEvent();
            }
        }
        void dump(const std::string &pref) const
        {
            auto& vec = m_pq.get_c();
            std::for_each(vec.begin(), vec.end(),
                [&pref](auto& t) { t.dump(pref); }
            );
        }
    };
}

