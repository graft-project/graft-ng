#pragma once

#include "thread_pool/thread_pool.hpp"

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
        {
            decltype(auto) vars_cref = m_bt->getVars();
            decltype(auto) input_ref = m_bt->getInput();
            decltype(auto) output_ref = m_bt->getOutput();
            decltype(auto) h3_ref = m_bt->getHandler3();
            decltype(auto) ctx = m_bt->getCtx();

            try
            {
                Status status = h3_ref.worker_action(vars_cref, input_ref, ctx, output_ref);
                Context::LocalFriend::setLastStatus(ctx.local, status);
                if(Status::Ok == status && h3_ref.post_action || Status::Forward == status)
                {
                    input_ref.assign(output_ref);
                }
            }
            catch(const std::exception& e)
            {
                ctx.local.setError(e.what());
                input_ref.reset();
            }
            catch(...)
            {
                ctx.local.setError("unknown exeption");
                input_ref.reset();
            }
        }
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
