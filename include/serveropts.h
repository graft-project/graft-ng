#ifndef SERVEROPTS_H
#define SERVEROPTS_H

#include <string>

namespace graft {

struct ServerOpts
{
    std::string http_address;
    std::string coap_address;
    double http_connection_timeout;
    double upstream_request_timeout;
    int workers_count;
    int worker_queue_len;
    std::string cryptonode_rpc_address;
    int timer_poll_interval_ms;
};

}




#endif // SERVEROPTS_H
