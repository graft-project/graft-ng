#pragma once

#include "lib/graft/thread_pool/thread_pool.hpp"

namespace graft {

////////
///
/// prototype of a job
///
template <typename BT_ptr, typename ResQueue, typename Watcher>
class GraftJob
{
public:
    explicit GraftJob(BT_ptr bt, ResQueue* rq, Watcher* watcher)
        : m_bt(bt)
        , m_rq(rq)
        , m_watcher(watcher)
    {}

    GraftJob(GraftJob&& rhs) noexcept
    {
        *this = std::move(rhs);
    }

    virtual ~GraftJob() = default;

    GraftJob& operator = (GraftJob&& rhs) noexcept
    {
        if(this != &rhs)
        {
            m_bt = std::move(rhs.m_bt);
            m_rq = std::move(rhs.m_rq);
            m_watcher = std::move(rhs.m_watcher);
        }
        return *this;
    }

    //main payload
    virtual void operator () ()
    {
        // Please read the comment about exceptions and noexcept specifier
        // near 'void terminate()' function in main.cpp
        m_bt->getManager().runWorkerActionFromTheThreadPool(m_bt);

        Watcher* save_m_watcher = m_watcher; //save m_watcher before move itself into resulting queue
        m_rq->push(std::move(*this)); //similar to "delete this;"
        save_m_watcher->notifyJobReady();
    }

    BT_ptr& getTask() { return m_bt; }
protected:
    BT_ptr m_bt;

    ResQueue* m_rq = nullptr;
    Watcher* m_watcher = nullptr;
};

}//namespace graft
