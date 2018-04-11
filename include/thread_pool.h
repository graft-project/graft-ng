#pragma once

#include "thread_pool/thread_pool.hpp"

using namespace tp;

///////
//time measurement helpers
using ticks_t = std::chrono::high_resolution_clock::duration;
using ticks_p = decltype(std::chrono::high_resolution_clock::now());
#define ticks_now() (std::chrono::high_resolution_clock::now())
using fp_ms = std::chrono::duration<double, std::milli>;

////////
///
/// prototype of a job
///
template <typename CR_ptr, typename Input, typename ResQueue, typename Watcher, typename Output>
class GraftJob
{
public:

	explicit GraftJob()
	{}

	explicit GraftJob(CR_ptr cr, Input&& in, ResQueue* rq, Watcher* watcher) //, Output out)
		: cr(cr)
		, m_in(in)
		, rq(rq)
		, watcher(watcher)
	{}

	GraftJob(GraftJob&& rhs) noexcept
	{
		*this = std::move(rhs);
	}


	GraftJob& operator=(GraftJob&& rhs) noexcept
	{
		if(this != &rhs)
		{
			cr = std::move(rhs.cr);
			m_in = std::move(rhs.m_in);
			m_out = std::move(rhs.m_out);
			rc = std::move(rhs.rc);
//			promise = std::move(rhs.promise);
			rq = std::move(rhs.rq);
			watcher = std::move(rhs.watcher);
		}
		return *this;
	}

	//main payload
	virtual void operator()()
	{
		auto begin_count = ticks_now();
		{
/*			
			m_out = m_in;
			while(std::next_permutation(m_out.begin(), m_out.end()))
			{
	//			std::cout << "x " << m_out << "\n";
			}
*/
			m_in.handler(m_in.vars, m_in.input, m_out);
		}

		auto end_count = ticks_now();
		rc = end_count - begin_count;

//		promise.set_value();

		Watcher* save_watcher = watcher; //save watcher before move itself into resulting queue
		rq->push(std::move(*this)); //similar to "delete this;"
		save_watcher->notifyJobReady();
	}

	Output getOutput() { return m_out; }
	ticks_t getReturnCode() { return rc; }

	virtual ~GraftJob() = default;
public:
	CR_ptr cr;
protected:
	Input m_in;
	Output m_out;

	ticks_t rc = ticks_t::zero();
	ResQueue* rq = nullptr;
	Watcher* watcher = nullptr;

//	std::promise<void> promise;

	char tmp_buf[1024]; //suppose polimorphic increase in size, so it is not good idea to move it from queue to queue,
	//but unique_ptr of it instead. see GJ_ptr.
};

