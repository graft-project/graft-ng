#pragma once

#include "thread_pool/thread_pool.hpp"

namespace graft {

////////
///
/// prototype of a job
///
template <typename CR_ptr, typename Input, typename ResQueue, typename Watcher, typename Output>
class GraftJob
{
public:
	explicit GraftJob(CR_ptr cr, Input&& in, ResQueue* rq, Watcher* watcher)
		: m_cr(cr)
		, m_in(in)
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
			m_in = std::move(rhs.m_in);
			m_out = std::move(rhs.m_out);
			m_rq = std::move(rhs.m_rq);
			m_watcher = std::move(rhs.m_watcher);
		}
		return *this;
	}

	//main payload
	virtual void operator () ()
	{
		{
			m_in.handler(m_in.vars, m_in.input, m_out);
		}
		Watcher* save_m_watcher = m_watcher; //save m_watcher before move itself into resulting queue
		m_rq->push(std::move(*this)); //similar to "delete this;"
		save_m_watcher->notifyJobReady();
	}

	Output get_Output() { return m_out; }
	CR_ptr& get_CR() { return m_cr; }
protected:
	CR_ptr m_cr;

	Input m_in;
	Output m_out;

	ResQueue* m_rq = nullptr;
	Watcher* m_watcher = nullptr;
};

}//namespace graft
