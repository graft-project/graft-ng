
#include "lib/graft/sys_info.h"

namespace graft::request::system_info {

Counter::Counter(void)
: m_http_req_total_cnt(0)
, m_http_req_routed_cnt(0)
, m_http_req_unrouted_cnt(0)
, m_http_resp_status_ok_cnt(0)
, m_http_resp_status_error_cnt(0)
, m_http_resp_status_drop_cnt(0)
, m_http_resp_status_busy_cnt(0)
, m_http_req_bytes_raw_cnt(0)
, m_http_resp_bytes_raw_cnt(0)
, m_upstrm_http_req_cnt(0)
, m_upstrm_http_resp_ok_cnt(0)
, m_upstrm_http_resp_err_cnt(0)
, m_upstrm_http_req_bytes_raw_cnt(0)
, m_upstrm_http_resp_bytes_raw_cnt(0)
, m_system_start_time(std::chrono::system_clock::now())
{
}

Counter::~Counter(void)
{
}

}

