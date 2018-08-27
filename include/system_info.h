#pragma once

#include <atomic>
#include <cstdint>

namespace graft { namespace supernode {

using u64 = std::uint64_t;

class ISystemInfoConsumer
{
  public:
    virtual u64 http_request_total_cnt(void) = 0;
    virtual u64 http_request_routed_cnt(void) = 0;
    virtual u64 http_request_unrouted_cnt(void) = 0;
};

class ISystemInfoProducer
{
  public:
    virtual void count_http_request_total(void) = 0;
    virtual void count_http_request_routed(void) = 0;
    virtual void count_http_request_unrouted(void) = 0;
};

class SystmeInfoProvider : public ISystemInfoConsumer, public ISystemInfoProducer
{
  public:
    SystmeInfoProvider(void);
    ~SystmeInfoProvider(void);

  public:
    void count_http_request_total(void) override { ++http_req_total_cnt_; }
    void count_http_request_routed(void) override { ++http_req_routed_cnt_; }
    void count_http_request_unrouted(void) override { ++http_req_unrouted_cnt_; }

  public:
    u64 http_request_total_cnt(void) override { return http_req_total_cnt_; }
    u64 http_request_routed_cnt(void) override { return http_req_routed_cnt_; }
    u64 http_request_unrouted_cnt(void) override { return http_req_unrouted_cnt_; }

  private:
    std::atomic<u64>  http_req_total_cnt_;
    std::atomic<u64>  http_req_routed_cnt_;
    std::atomic<u64>  http_req_unrouted_cnt_;
};

} }
