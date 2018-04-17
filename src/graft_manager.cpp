#include "graft_manager.h"

namespace graft {

void Manager::sendCrypton(ClientRequest_ptr cr)
{
	++m_cntCryptoNodeSender;
	CryptoNodeSender::Ptr cns = CryptoNodeSender::Create();
	std::string something(100, ' ');
	{
		std::string s("something");
		for(int i=0; i< s.size(); ++i)
		{
			something[i] = s[i];
		}
	}
	cns->send(*this, cr, something );
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
	gj.get_CR()->onJobDone(gj);
	++m_cntJobDone;
	//gj will be destroyed on exit
}

void Manager::onCryptonDone(CryptoNodeSender& cns)
{
	cns.get_CR()->onCryptonDone(cns);
	++m_cntCryptoNodeSenderDone;
	//cns will be destroyed on exit
}

}//namespace graft
