#include <gtest/gtest.h>
#include <functional>
#include "lib/graft/thread_pool/thread_pool.hpp"

namespace detail
{
inline size_t next_pow2(size_t val)
{
    --val;
    for(size_t i = 1; i<sizeof(val)*8; i<<=1)
    {
        val |= val >> i;
    }
    return ++val;
}
} //namespace detail

TEST(ThreadPool, expelling)
{
    std::atomic<int> fast_done_cnt = 0;
    auto fast = [&fast_done_cnt](std::atomic<int>& s, int i)->std::function<void()>
    {
        return [&fast_done_cnt,&s,i]()->void
        {
            s += i;
            ++fast_done_cnt;
        };
    };

    std::atomic<int> slow_done_cnt = 0;
    auto slow = [&slow_done_cnt](int ms)->std::function<void()>
    {
        return [&slow_done_cnt,ms]()->void
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(ms));
            ++slow_done_cnt;
        };
    };

    const int expel_interval = 500;
    tp::ThreadPoolOptions th_op;
    th_op.setThreadCount(std::thread::hardware_concurrency());
    th_op.setQueueSize(1024);
    th_op.setExpellingIntervalMs(expel_interval);

    constexpr size_t ffSize = std::max( sizeof( decltype(slow(1)) ), sizeof( decltype(fast(std::declval<std::atomic<int>&>(), 1)) ) );
    using ThPool = tp::ThreadPoolImpl<tp::FixedFunction<void(), ffSize>, tp::MPMCBoundedQueue>;

    std::unique_ptr<ThPool> thPool = std::make_unique<ThPool>(th_op);

    const int slow_cnt = 100;
    const int fast_per_slow = 10;
    const int K = 2*expel_interval/slow_cnt;
    std::atomic<int> s = 0;
    for(int i = 0; i < slow_cnt; ++i)
    {
        std::function<void()> slow_func = slow(K*(slow_cnt-1-i)+1);
        thPool->post(slow_func, true);
        for(int j = 0; j<fast_per_slow; ++j)
        {
            std::function<void()> fast_func = fast(s, i+1);
            thPool->post(fast_func, true);
        }
    }

    std::atomic<bool> worker_checker_stop = false;
    std::thread worker_checker ( [&thPool,&worker_checker_stop]()
    {
        while(!worker_checker_stop)
        {
            thPool->expelWorkers();
        }
    });

    while(slow_cnt != slow_done_cnt)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    worker_checker_stop = true;
    worker_checker.join();

    {
        uint64_t active = thPool->getActiveWorkersCount();
        uint64_t expelled = thPool->getExpelledWorkersCount();

        EXPECT_EQ(expelled != 0, true);
        EXPECT_EQ(active, th_op.threadCount());
    }
    thPool.reset();

    {
        uint64_t active = thPool->getActiveWorkersCount();
        uint64_t expelled = thPool->getExpelledWorkersCount();

        EXPECT_EQ(expelled != 0, true);
        int expect = slow_cnt/2;
        EXPECT_EQ(expect*.97 < expelled && expelled < expect*1.3, true);
        EXPECT_EQ(active, 0);
        std::cout << "\nexpelled = " << expelled << "\n";
    }
    EXPECT_EQ(s, fast_per_slow * (slow_cnt+1) * slow_cnt /2 );
}
