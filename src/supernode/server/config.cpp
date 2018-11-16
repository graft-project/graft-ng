
#include "supernode/server/config.h"

namespace graft::supernode::server {

Config::Config(void)
: http_connection_timeout(0)
, upstream_request_timeout(0)
, workers_count(0)
, worker_queue_len(0)
, timer_poll_interval_ms(0)
, log_trunc_to_size(0)
, lru_timeout_ms(0)
, log_console(false)
{}

}

