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
    // data directory - base directory where supernode stake wallet and other supernodes wallets are located
    std::string data_dir;
    // testnet flag
    bool testnet;
    // runtime parameters.
    // path to watch-only wallets (supernodes)
    std::string watchonly_wallets_path;
};

}




#endif // SERVEROPTS_H
