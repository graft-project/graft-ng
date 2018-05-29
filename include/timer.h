#pragma once

#include <chrono>
#include <boost/heap/priority_queue.hpp>
#include <functional>
#include <iostream>
#include <string>

namespace graft
{
    namespace ch = std::chrono;
    using Func = std::function<void(void)>;
/*
    template<typename Rep, typename Period>
    struct posix_duration_cast< ch::duration<Rep, Period>, struct timeval >
    {
        static struct timeval cast(ch::duration<Rep, Period> const& d)
        {
            struct timeval tv;
            ch::seconds const sec = ch::duration_cast<ch::seconds>(d);

            tv.tv_sec  = sec.count();
            tv.tv_usec = ch::duration_cast<ch::microseconds>(d - sec).count();

            return std::move(tv);
        }
    };
*/
    class TimerList
    {
        struct timer
        {
            ch::seconds timeout;
            ch::seconds lap;
            Func f;

            timer(ch::seconds _timeout, Func _f)
                : timeout(_timeout)
                , lap(ch::seconds::max())
                , f(_f) {}

            timer() = delete;

            void start()
            {
                lap = ch::time_point_cast<ch::seconds>(
                    ch::steady_clock::now()
                ).time_since_epoch() + timeout;
            }

            bool fired()
            {
                return lap <=
                    ch::time_point_cast<ch::seconds>(
                        ch::steady_clock::now()
                    ).time_since_epoch();
            }
/*
            struct timeval timeval()
            {
                return posix_duration_cast<ch::seconds>(lap);
            }
*/
            void dump(const std::string &pref)
            {
                std::cout << pref << " timeout: " << timeout.count()
                    << "; lap: " << lap.count() << std::endl;
            }
        };

        struct TimerCompare
        {
            bool operator()(
                    const std::shared_ptr<TimerList::timer> &t1,
                    const std::shared_ptr<TimerList::timer> &t2
            ) const
            {
                return t1->lap > t2->lap;
            }
        };

        boost::heap::priority_queue<
            std::shared_ptr<TimerList::timer>,
            boost::heap::compare<TimerCompare>> m_pq;

    public:
        void push(ch::seconds timeout, Func func) { m_pq.emplace(new timer(timeout, func)); }
        void eval()
        {
            auto t = m_pq.top();
            while (t->fired())
            {
                m_pq.pop();
                t->start();
                m_pq.emplace(t);
                if(t->f) t->f();
            }
        }

        void start()
        {
            std::for_each(m_pq.begin(), m_pq.end(),
                [](const std::shared_ptr<TimerList::timer>& t) { t->start(); }
            );
        }

        void dump(const std::string &pref)
        {
            std::for_each(m_pq.begin(), m_pq.end(),
                [&](const std::shared_ptr<TimerList::timer>& t)
                {
                    t->dump(pref);
                }
            );
        }
    };
}

