#pragma once

#include <string>

namespace graft
{

struct ConfigOpts
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
    int lru_timeout_ms;
    // testnet flag
    bool testnet;
    std::string stake_wallet_name;
    size_t stake_wallet_refresh_interval_ms;
    // runtime parameters.
    // path to watch-only wallets (supernodes)
    std::string watchonly_wallets_path;

    int log_level;
    bool log_console;
    std::string log_filename;
};

}

