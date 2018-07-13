#pragma once

#include "thread_pool/thread_pool.hpp"

namespace graft {

////////
///
/// prototype of a job
///
template <typename CR_ptr, typename ResQueue, typename Watcher>
class GraftJob
{
public:
    explicit GraftJob(CR_ptr cr, ResQueue* rq, Watcher* watcher)
        : m_cr(cr)
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
            m_cr = std::move(rhs.m_cr);
            m_rq = std::move(rhs.m_rq);
            m_watcher = std::move(rhs.m_watcher);
        }
        return *this;
    }

    //main payload
    virtual void operator () ()
    {
        {
            decltype(auto) vars_cref = m_cr->get_vars();
            decltype(auto) input_ref = m_cr->get_input();
            decltype(auto) output_ref = m_cr->get_output();
            decltype(auto) h3_ref = m_cr->get_h3();
            decltype(auto) ctx = m_cr->get_ctx();

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
                ctx.local.setError("unknown exception");
                input_ref.reset();
            }
        }
        Watcher* save_m_watcher = m_watcher; //save m_watcher before move itself into resulting queue
        m_rq->push(std::move(*this)); //similar to "delete this;"
        save_m_watcher->notifyJobReady();
    }

    CR_ptr& get_cr() { return m_cr; }
protected:
    CR_ptr m_cr;

    ResQueue* m_rq = nullptr;
    Watcher* m_watcher = nullptr;
};

}//namespace graft
