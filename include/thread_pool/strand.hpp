#pragma once

#include <thread_pool/thread_pool.hpp>

#include <atomic>
#include <cassert>

namespace tp
{

template <typename Task, template<typename> class Queue>
class StrandImpl;
using Strand = StrandImpl<FixedFunction<void(), 128>,
                                  MPMCBoundedQueue>;

/**
 * @brief The StrandImpl class implements serialized handler execution.
 * It is header only.
 */
template <typename Task, template<typename> class Queue>
class StrandImpl
{
public:
    typedef ThreadPoolImpl<Task, Queue> ThreadPool;

    /// Constructor
    ///
    /// @param thread_pool Linked thread pool
    /// @param queue_size Size of queue for handlers
    StrandImpl(ThreadPool& thread_pool, size_t queue_size);

    /** 
      * @brief Post handler to be executed in a sequential order in a thread pool
      *
      * @param pool Target thread pool
      * @param handler Handler to be called from thread pool worker. It has to be
      * callable as 'handler()'.
      * @param to_any_queue If true, attempts to post into each worker queue
      * until success. Throws the exception otherwise. If false only one
      * attempt will be made.
      * @throw std::overflow_error if worker's queue is full.
      * @note All exceptions thrown by handler will be suppressed.
      */
    template <typename Handler>
    void post(Handler&& handler, bool to_any_queue = false);

private:
    void invokeCall();

    class StrandImplHandler
    {
    public:
        StrandImplHandler(StrandImpl& StrandImpl) : m_StrandImpl(&StrandImpl) {}

        void operator ()() const { m_StrandImpl->invokeCall(); }

    private:
        StrandImpl* m_StrandImpl;
    };

private:
    ThreadPool& m_thread_pool;
    Queue<Task> m_queue;
    std::atomic<size_t> m_deferred_calls_count;
};

template <typename Task, template<typename> class Queue>
StrandImpl<Task, Queue>::StrandImpl(ThreadPool& thread_pool, size_t queue_size)
  : m_thread_pool(thread_pool)
  , m_queue(queue_size)
  , m_deferred_calls_count()
{
}

template <typename Task, template<typename> class Queue>
template <typename Handler>
void StrandImpl<Task, Queue>::post(Handler&& handler, bool to_any_queue)
{
    m_queue.push(std::forward<Handler>(handler));

    m_thread_pool.post(StrandImplHandler(*this), to_any_queue);
}

template <typename Task, template<typename> class Queue>
void StrandImpl<Task, Queue>::invokeCall()
{
    if (m_deferred_calls_count.fetch_add(1, std::memory_order_relaxed))
        return;

    Task handler;

    for (;;)
    {
        assert(m_queue.pop(handler));

        try
        {
            handler();
        }
        catch (...)
        {
            // suppress all exceptions
        }           
        
        bool has_deferred_calls = m_deferred_calls_count.fetch_sub(1, std::memory_order_relaxed) > 1;

        if (!has_deferred_calls)
          break;
    }
}

}
