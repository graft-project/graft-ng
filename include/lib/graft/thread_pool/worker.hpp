#pragma once

#include <atomic>
#include <thread>
#include <cassert>

namespace tp
{

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
    void start(size_t id, Queue<Task>& queue, Queue<Task>& steal_queue, std::shared_ptr<WorkerT>&& rwptr);

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

    void threadFunc(size_t id, Queue<Task>& queue, Queue<Task>& steal_queue, std::shared_ptr<WorkerT>&& rwptr);

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
inline void WorkerT<Task, Queue>::start(size_t id, Queue<Task>& queue, Queue<Task>& steal_queue, std::shared_ptr<WorkerT>&& rwptr)
{
    assert(rwptr.get() == this);
    ++activeCount;
    m_thread = std::thread([this,id,&queue,&steal_queue,rwptr]()
    {
        std::shared_ptr<WorkerT> wptr = rwptr;
        threadFunc(id, queue, steal_queue, std::move(wptr));
    });

}

template <typename Task, template<typename> class Queue>
inline size_t WorkerT<Task, Queue>::getWorkerIdForCurrentThread()
{
    return *detail::thread_id();
}

template <typename Task, template<typename> class Queue>
inline void WorkerT<Task, Queue>::threadFunc(size_t id, Queue<Task>& queue, Queue<Task>& steal_queue, std::shared_ptr<WorkerT>&& rwptr)
{
    assert(rwptr.get() == this);

    *detail::thread_id() = id;

    Task handler;

    while (m_running_flag.load(std::memory_order_relaxed))
    {
        if (queue.pop(handler) || steal_queue.pop(handler))
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
