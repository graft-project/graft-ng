#pragma once

#include <algorithm>
#include <thread>

namespace tp
{

/**
 * @brief The ThreadPoolOptions class provides creation options for
 * ThreadPool.
 */
class ThreadPoolOptions
{
public:
    /**
     * @brief ThreadPoolOptions Construct default options for thread pool.
     */
    ThreadPoolOptions();

    /**
     * @brief setThreadCount Set thread count.
     * @param count Number of threads to be created.
     */
    void setThreadCount(size_t count);

    /**
     * @brief setQueueSize Set single worker queue size.
     * @param count Maximum length of queue of single worker.
     */
    void setQueueSize(size_t size);

    /**
     * @brief threadCount Return thread count.
     */
    size_t threadCount() const;

    /**
     * @brief queueSize Return single worker queue size.
     */
    size_t queueSize() const;

    /**
     * @brief setExpellingIntervalMs Set default time interval per a job before creating substituting worker.
     * @param ms Value in milliseconds.
     */
    void setExpellingIntervalMs(size_t ms) { m_workers_expelling_interval_ms = ms; }

    /**
     * @brief expellingIntervalMs Return default time interval per a job before creating substituting worker.
     */
    size_t expellingIntervalMs() const { return m_workers_expelling_interval_ms; }

private:
    size_t m_thread_count;
    size_t m_queue_size;
    size_t m_workers_expelling_interval_ms;
};

/// Implementation

inline ThreadPoolOptions::ThreadPoolOptions()
    : m_thread_count(std::max<size_t>(2u, std::thread::hardware_concurrency()))
    , m_queue_size(1024u)
    , m_workers_expelling_interval_ms(1000u)
{
}

inline void ThreadPoolOptions::setThreadCount(size_t count)
{
    m_thread_count = std::max<size_t>(2u, count);
}

inline void ThreadPoolOptions::setQueueSize(size_t size)
{
    m_queue_size = std::max<size_t>(1u, size);
}

inline size_t ThreadPoolOptions::threadCount() const
{
    return m_thread_count;
}

inline size_t ThreadPoolOptions::queueSize() const
{
    return m_queue_size;
}

}
