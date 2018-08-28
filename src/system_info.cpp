#include "system_info.h"

namespace graft { namespace supernode {

SystemInfoProvider::SystemInfoProvider(void)
: m_http_req_total_cnt(0)
, m_http_req_routed_cnt(0)
, m_http_req_unrouted_cnt(0)
, m_server_start_time(std::chrono::system_clock::now())
{
}

SystemInfoProvider::~SystemInfoProvider(void)
{
}

} }

