
#pragma once

#include "lib/graft/thread_pool/fixed_function.hpp"
#include "lib/graft/thread_pool/mpmc_bounded_queue.hpp"
#include "lib/graft/thread_pool/thread_pool_options.hpp"
#include "lib/graft/thread_pool/worker.hpp"

#include <atomic>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>
#include <cassert>

namespace tp
{

template <typename Task, template<typename> class Queue>
class ThreadPoolImpl;
using ThreadPool = ThreadPoolImpl<FixedFunction<void(), 128>,
                                  MPMCBoundedQueue>;

/**
 * @brief The ThreadPool class implements thread pool pattern.
 * It is highly scalable and fast.
 * It is header only.
 * It implements both work-stealing and work-distribution balancing
 * startegies.
 * It implements cooperative scheduling strategy for tasks.
 */
template <typename Task, template<typename> class Queue>
class ThreadPoolImpl {
public:
    /**
     * @brief ThreadPool Construct and start new thread pool.
     * @param options Creation options.
     */
    explicit ThreadPoolImpl(
        const ThreadPoolOptions& options = ThreadPoolOptions());

    /**
     * @brief Move ctor implementation.
     */
    ThreadPoolImpl(ThreadPoolImpl&& rhs) noexcept;

    /**
     * @brief ~ThreadPool Stop all workers and destroy thread pool.
     */
    ~ThreadPoolImpl();

    /**
     * @brief Move assignment implementaion.
     */
    ThreadPoolImpl& operator=(ThreadPoolImpl&& rhs) noexcept;

    /**
     * @brief post Try post job to thread pool.
     * @param handler Handler to be called from thread pool worker. It has
     * to be callable as 'handler()'.
     * @return 'true' on success, false otherwise.
     * @note All exceptions thrown by handler will be suppressed.
     */
    template <typename Handler>
    bool tryPost(Handler&& handler);

    /**
     * @brief post Post job to thread pool.
     * @param handler Handler to be called from thread pool worker. It has
     * to be callable as 'handler()'.
     * @param to_any_queue If true, attempts to post into each worker queue
     * until success. Throws the exception otherwise. If false only one
     * attempt will be made.
     * @throw std::overflow_error if worker's queue is full.
     * @note All exceptions thrown by handler will be suppressed.
     */
    template <typename Handler>
    void post(Handler&& handler, bool to_any_queue = false);

    int dump_info()
    {
        int cnt = 0;
        std::cout << "\nThread pool dump\n";
        for(auto& worker_ptr : m_workers)
        {
            std::cout << "\t";
            cnt += worker_ptr->dump();
        }
        return cnt;
    }

    //it is for a single thread
    void expelWorkers();

    static uint64_t getActiveWorkersCount();
    static uint64_t getExpelledWorkersCount();

private:
    size_t getWorkerIdx();

    using Worker = WorkerT<Task, Queue>;
    using TimePoint = typename Worker::TimePoint;
    using QueuesVec = std::vector<Queue<Task>>;
    using WorkersVec = std::vector<std::shared_ptr<Worker>>;

    std::unique_ptr<std::vector<Queue<Task>>> m_queues;
    std::unique_ptr<std::vector<std::shared_ptr<Worker>>> m_workers;

    std::atomic<size_t> m_next_worker = 0;
};


/// Implementation

template <typename Task, template<typename> class Queue>
inline ThreadPoolImpl<Task, Queue>::ThreadPoolImpl(
                                            const ThreadPoolOptions& options)
{
    using Milliseconds = typename Worker::Milliseconds;
    Worker::defaultPeriodMs = Milliseconds(options.expellingIntervalMs());

    m_queues = std::make_unique<QueuesVec>();
    m_queues->reserve(options.threadCount());
    m_workers = std::make_unique<WorkersVec>();
    m_workers->reserve(options.threadCount());

    QueuesVec& queues = *m_queues;
    WorkersVec& workers = *m_workers;

    for(size_t i = 0; i < options.threadCount(); ++i)
    {
        queues.emplace_back(Queue<Task>(options.queueSize()));
        workers.emplace_back(std::make_shared<Worker>());
    }

    for(size_t i = 0; i < workers.size(); ++i)
    {
        size_t i1 = (i + 1) % workers.size();
        std::shared_ptr wrkr(workers[i]);
        workers[i]->start(i, queues[i], queues[i1], std::move(wrkr));
    }
}

//this function should be called by a single thread per ThreadPool only
template <typename Task, template<typename> class Queue>
inline void ThreadPoolImpl<Task, Queue>::expelWorkers()
{
    TimePoint now = Worker::getTimePoint();

    QueuesVec& queues = *m_queues;
    WorkersVec& workers = *m_workers;

    for(size_t i = 0; i < workers.size(); ++i)
    {
        if(now < workers[i]->m_timePoint.load()) continue;
        auto oworker = workers[i];
        oworker->m_running_flag = false;
        oworker->m_thread.detach();

        std::shared_ptr<Worker> nworker = std::make_shared<Worker>();
        workers[i] = nworker;
        size_t i1 = (i + 1) % workers.size();
        workers[i]->start(i, queues[i], queues[i1], std::move(nworker));

        ++Worker::expelledCount;
    }
}

template <typename Task, template<typename> class Queue>
inline uint64_t ThreadPoolImpl<Task, Queue>::getActiveWorkersCount()
{
    return Worker::activeCount;
}

template <typename Task, template<typename> class Queue>
inline uint64_t ThreadPoolImpl<Task, Queue>::getExpelledWorkersCount()
{
    return Worker::expelledCount;
}

template <typename Task, template<typename> class Queue>
inline ThreadPoolImpl<Task, Queue>::ThreadPoolImpl(ThreadPoolImpl<Task, Queue>&& rhs) noexcept
{
    *this = std::forward<ThreadPoolImpl<Task, Queue>>(rhs);
}

template <typename Task, template<typename> class Queue>
inline ThreadPoolImpl<Task, Queue>::~ThreadPoolImpl()
{
    if(!m_workers) return;
    //it is expected that a caller of this dtor checks somehow before calling that the thread pool is empty,
    //more strictly that all jobs are done
    for (auto& worker_ptr : *m_workers)
    {
        worker_ptr->stop();
    }

    while(Worker::activeCount)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    assert((Worker::activeCount == 0));
}

template <typename Task, template<typename> class Queue>
inline ThreadPoolImpl<Task, Queue>&
ThreadPoolImpl<Task, Queue>::operator=(ThreadPoolImpl<Task, Queue>&& rhs) noexcept
{
    if (this != &rhs)
    {
        m_queues = std::move(rhs.m_queues);
        m_workers = std::move(rhs.m_workers);
        m_next_worker = rhs.m_next_worker.load();
    }
    return *this;
}

template <typename Task, template<typename> class Queue>
template <typename Handler>
inline bool ThreadPoolImpl<Task, Queue>::tryPost(Handler&& handler)
{
    return (*m_queues)[getWorkerIdx()].push(std::forward<Handler>(handler));
}

template <typename Task, template<typename> class Queue>
template <typename Handler>
inline void ThreadPoolImpl<Task, Queue>::post(Handler&& handler, bool to_any_queue)
{
    int try_count = (to_any_queue)? m_workers->size() : 1;
    for(int i = 0; i < try_count; ++i)
    {
        bool ok = tryPost(std::forward<Handler>(handler));
        if(ok) return;
    }
    throw std::runtime_error("thread pool queue is full");
}

template <typename Task, template<typename> class Queue>
inline size_t ThreadPoolImpl<Task, Queue>::getWorkerIdx()
{
    auto id = Worker::getWorkerIdForCurrentThread();

    if (id > m_workers->size())
    {
        id = m_next_worker.fetch_add(1, std::memory_order_relaxed) %
             m_workers->size();
    }

    return id;
}
}
