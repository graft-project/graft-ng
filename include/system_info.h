#pragma once

#include <atomic>
#include <cstdint>
#include <chrono>

namespace graft { namespace supernode {

using u32 = std::uint32_t;
using u64 = std::uint64_t;
using SysClockTimePoint = std::chrono::time_point<std::chrono::system_clock>;

class SystemInfoProvider
{
  public:
    SystemInfoProvider(void);
    ~SystemInfoProvider(void);

  // interface for producer
    void count_http_request_total(void) { ++m_http_req_total_cnt; }
    void count_http_request_routed(void) { ++m_http_req_routed_cnt; }
    void count_http_request_unrouted(void) { ++m_http_req_unrouted_cnt; }

  // interface for consumer
    u64 http_request_total_cnt(void) const { return m_http_req_total_cnt; }
    u64 http_request_routed_cnt(void) const { return m_http_req_routed_cnt; }
    u64 http_request_unrouted_cnt(void) const { return m_http_req_unrouted_cnt; }

    u32 server_uptime_sec(void) const
    {
        return std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now() - m_server_start_time).count();
    }

  private:
    std::atomic<u64>  m_http_req_total_cnt;
    std::atomic<u64>  m_http_req_routed_cnt;
    std::atomic<u64>  m_http_req_unrouted_cnt;

    const SysClockTimePoint m_server_start_time;
};

} }
