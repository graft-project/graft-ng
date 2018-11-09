#pragma once

#include <string>
#include <vector>

namespace graft {

struct ConfigOpts
{
    std::string config_filename;
    std::string http_address;
    std::string coap_address;
    double http_connection_timeout;
    double upstream_request_timeout;
    int workers_count;
    int worker_queue_len;
    std::string cryptonode_rpc_address;
    int timer_poll_interval_ms;
    int log_trunc_to_size;
    std::vector<std::string> graftlet_dirs;
    int lru_timeout_ms;
};

}
