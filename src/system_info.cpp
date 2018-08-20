#include "system_info.h"
#include "context.h"

namespace graft { namespace supernode {

SystemInfoProvider::SystemInfoProvider(void)
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

SystemInfoProvider::~SystemInfoProvider(void)
{
}

//SystemInfoProvider& get_system_info_provider_from_ctx(const graft::Context& ctx)
//{
//    graft::supernode::SystemInfoProvider* psip = ctx.global.operator[]<graft::supernode::SystemInfoProvider*>("system_info_provider");
//    //graft::supernode::SystemInfoProvider* psip = ctx.global.get("system_info_provider", nullptr);
//      //ctx.global.operator[]<graft::supernode::ISystemInfoProvider*>("system_info_provider");
//    assert(psip);
//    return *psip;
//}

} }

