#include "graft_manager.h"

namespace graft {

#ifdef OPT_BUILD_TESTS
std::atomic_bool GraftServer::ready = false;
#endif


void Manager::sendCrypton(ClientRequest_ptr cr)
{
	++m_cntCryptoNodeSender;
	CryptoNodeSender::Ptr cns = CryptoNodeSender::Create();
	cns->send(*this, cr, cr->get_input());
}

void Manager::sendToThreadPool(ClientRequest_ptr cr)
{
	assert(m_cntJobDone <= m_cntJobSent);
	if(m_cntJobDone - m_cntJobSent == m_threadPoolInputSize)
	{//check overflow
		processReadyJobBlock();
	}
	assert(m_cntJobDone - m_cntJobSent < m_threadPoolInputSize);
	++m_cntJobSent;
	cr->createJob(*this);
}

bool Manager::tryProcessReadyJob()
{
	GJ_ptr gj;
	bool res = get_resQueue().pop(gj);
	if(!res) return res;
	onJobDone(*gj);
	return true;
}

void Manager::processReadyJobBlock()
{
	while(true)
	{
		bool res = tryProcessReadyJob();
		if(res) break;
	}
}

void Manager::doWork(uint64_t m_cnt)
{
	//job overflow is possible, and we can pop jobs without notification, thus m_cnt useless
	bool res = true;
	while(res)
	{
		res = tryProcessReadyJob();
		if(!res) break;
	}
}

void Manager::onJobDone(GJ& gj)
{
	gj.get_cr()->onJobDone(gj);
	++m_cntJobDone;
	//gj will be destroyed on exit
}

void Manager::onCryptonDone(CryptoNodeSender& cns)
{
	cns.get_cr()->onCryptonDone(cns);
	++m_cntCryptoNodeSenderDone;
	//cns will be destroyed on exit
}

}//namespace graft
