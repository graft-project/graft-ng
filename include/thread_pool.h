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
			decltype(auto) status_ref = m_cr->get_StatusRef();
			decltype(auto) vars_cref = m_cr->get_Vars();
			decltype(auto) input_cref = m_cr->get_Input();
			decltype(auto) output_ref = m_cr->get_Output();
			decltype(auto) handler_ref = m_cr->get_Handler();

			status_ref = handler_ref(vars_cref, input_cref, output_ref);
		}
		Watcher* save_m_watcher = m_watcher; //save m_watcher before move itself into resulting queue
		m_rq->push(std::move(*this)); //similar to "delete this;"
		save_m_watcher->notifyJobReady();
	}

	CR_ptr& get_CR() { return m_cr; }
protected:
	CR_ptr m_cr;

	ResQueue* m_rq = nullptr;
	Watcher* m_watcher = nullptr;
};

}//namespace graft
