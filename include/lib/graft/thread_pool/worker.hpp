#pragma once

#include <atomic>
#include <thread>
#include <functional>
#include <cassert>

namespace graft
{

class Iter421
{
    int m_cnt = 0;
/*
    void set_next_prior(int prior)
    {
        switch(prior)
        {
        case 0: m_cnt = 0; break;
        case 1: m_cnt = 3; break;
        case 2: m_cnt = 5; break;
        default: assert(false);
        }
    }
*/
    int get_next_prior()
    {
        if(m_cnt == 7) m_cnt = 0;
        int v = m_cnt++;
        if(v < 4) return 0;
        if(v < 6) return 1;
        return 2;
    }
    int get_next_prior(int start_from_prior)
    {
        switch(start_from_prior)
        {
        case 0: m_cnt = 0; break;
        case 1: m_cnt = 4; break;
        case 2: m_cnt = 6; break;
        default: assert(false);
        }
        return get_next_prior();
    }
public:
    static const size_t prioritiesSize = 3;

    void reset(){ m_cnt = 0; }
    bool do_prior(std::function<bool(int prior)> f)
    {
        int start_prior = get_next_prior();
        for(int prior = start_prior;;)
        {
            bool res = f(prior);
            if(res) return true;
            prior = (++prior)%3;
            if(prior == start_prior) return false;
            prior  = get_next_prior(prior);
        }
    }
};

} //namespace graft

namespace tp
{

const size_t prioritiesSize = graft::Iter421::prioritiesSize;
using Iter421 = graft::Iter421;

/**
 * @brief The WorkerT class owns task queue and executing thread.
 * In thread it tries to pop task from queue. If queue is empty then it tries
 * to steal task from the sibling worker. If steal was unsuccessful then spins
 * with one millisecond delay.
 */
template <typename Task, template<typename> class Queue>
class WorkerT
{
public:
    using QueueArr = std::array<Queue<Task>, prioritiesSize>;

    using TimePoint = std::chrono::high_resolution_clock::time_point;
    using Milliseconds = std::chrono::milliseconds;

    static TimePoint maxTimePoint()
    {
        return std::chrono::high_resolution_clock::time_point::max();
    }

    static TimePoint getTimePoint(Milliseconds d = Milliseconds(0))
    {
        TimePoint tp = std::chrono::high_resolution_clock::now();
        tp += d;
        return tp;
    }

    /**
     * @brief WorkerT Constructor.
     * @param queue_size Length of undelaying task queue.
     */
    WorkerT() noexcept : m_timePoint(maxTimePoint()) { }

    /**
     * @brief Move ctor implementation.
     */
    WorkerT(WorkerT&& rhs) noexcept;

    /**
     * @brief Move assignment implementaion.
     */
    WorkerT& operator=(WorkerT&& rhs) noexcept;

    /**
     * @brief start Create the executing thread and start tasks execution.
     * @param id WorkerT ID.
     * @param steal_donor Sibling worker to steal task from it.
     */
    void start(size_t id, QueueArr& queueArr, QueueArr& steal_queueArr, std::shared_ptr<WorkerT>&& rwptr);
//    void start(size_t id, Queue<Task>& queue, Queue<Task>& steal_queue, std::shared_ptr<WorkerT>&& rwptr);

    /**
     * @brief stop Stop all worker's thread and stealing activity.
     * Waits until the executing thread became finished.
     */
    void stop();

    /**
     * @brief getWorkerIdForCurrentThread Return worker ID associated with
     * current thread if exists.
     * @return WorkerT ID.
     */
    static size_t getWorkerIdForCurrentThread();

    /**
     * @brief threadFunc Executing thread function.
     * @param id WorkerT ID to be associated with this thread.
     * @param steal_donor Sibling worker to steal task from it.
     */

    void threadFunc(size_t id, QueueArr& queueArr, QueueArr& steal_queueArr, std::shared_ptr<WorkerT>&& rwptr);

    static_assert(std::atomic<uint64_t>::is_always_lock_free);
    static std::atomic<uint64_t> activeCount;
    static std::atomic<uint64_t> expelledCount;
    static std::chrono::milliseconds defaultPeriodMs;

    std::atomic<TimePoint> m_timePoint = maxTimePoint();
    static_assert(decltype(m_timePoint)::is_always_lock_free);
    std::atomic<bool> m_running_flag{true};
    std::thread m_thread;
};


/// Implementation

namespace detail
{
    inline size_t* thread_id()
    {
        static thread_local size_t tss_id = -1u;
        return &tss_id;
    }
}

template <typename Task, template<typename> class Queue>
std::atomic<uint64_t> WorkerT<Task, Queue>::activeCount = 0;

template <typename Task, template<typename> class Queue>
std::atomic<uint64_t> WorkerT<Task, Queue>::expelledCount = 0;

template <typename Task, template<typename> class Queue>
std::chrono::milliseconds WorkerT<Task, Queue>::defaultPeriodMs(200);

template <typename Task, template<typename> class Queue>
inline WorkerT<Task, Queue>::WorkerT(WorkerT&& rhs) noexcept
{
    *this = rhs;
}

template <typename Task, template<typename> class Queue>
inline WorkerT<Task, Queue>& WorkerT<Task, Queue>::operator=(WorkerT&& rhs) noexcept
{
    if (this != &rhs)
    {
        m_running_flag = rhs.m_running_flag.load();
        m_thread = std::move(rhs.m_thread);
    }
    return *this;
}

template <typename Task, template<typename> class Queue>
inline void WorkerT<Task, Queue>::stop()
{
    m_running_flag.store(false, std::memory_order_relaxed);
    m_thread.join();
}

template <typename Task, template<typename> class Queue>
//inline void WorkerT<Task, Queue>::start(size_t id, Queue<Task>& queue, Queue<Task>& steal_queue, std::shared_ptr<WorkerT>&& rwptr)
inline void WorkerT<Task, Queue>::start(size_t id, WorkerT<Task, Queue>::QueueArr& queueArr, WorkerT<Task, Queue>::QueueArr& steal_queueArr, std::shared_ptr<WorkerT>&& rwptr)
{
    assert(rwptr.get() == this);
    ++activeCount;
    m_thread = std::thread([this,id,&queueArr,&steal_queueArr,rwptr]()
    {
        std::shared_ptr<WorkerT> wptr = rwptr;
        threadFunc(id, queueArr, steal_queueArr, std::move(wptr));
    });

}

template <typename Task, template<typename> class Queue>
inline size_t WorkerT<Task, Queue>::getWorkerIdForCurrentThread()
{
    return *detail::thread_id();
}

template <typename Task, template<typename> class Queue>
inline void WorkerT<Task, Queue>::threadFunc(size_t id, WorkerT<Task, Queue>::QueueArr& queueArr, WorkerT<Task, Queue>::QueueArr& steal_queueArr, std::shared_ptr<WorkerT>&& rwptr)
{
    assert(rwptr.get() == this);

    *detail::thread_id() = id;

    Iter421 iter;
    Task handler;

    while (m_running_flag.load(std::memory_order_relaxed))
    {
        bool res = iter.do_prior([&queueArr,&handler](int prior)->bool { return queueArr[prior].pop(handler); } );
        if(!res)
        {
            iter.reset();
            for(auto& q : steal_queueArr)
            {
                res = q.pop(handler);
                if(res) break;
            }
        }
//        if (queue.pop(handler) || steal_queue.pop(handler))
        if (res)
        {
            try
            {
                m_timePoint = getTimePoint(defaultPeriodMs);
                handler();
                m_timePoint = maxTimePoint();
            }
            catch(...)
            {
                throw;
            }
        }
        else
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    --activeCount;
}

}
