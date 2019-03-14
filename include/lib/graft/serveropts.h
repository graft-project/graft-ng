#pragma once

#include <string>
#include <vector>
#include <cassert>

namespace graft {

struct CommonOpts
{
    // testnet flag
    bool testnet;
    // data directory - base directory where supernode stake wallet and other supernodes wallets are located
    std::string data_dir;
    std::string wallet_public_address;
};

struct IPFilterOpts
{
    int requests_per_sec = 0;
    int window_size_sec = 0;
    int ban_ip_sec = 0;
    std::string rules_filename;
};

struct ConfigOpts
{
    std::string config_filename;
    std::string http_address;
    std::string coap_address;
    double http_connection_timeout;
    double upstream_request_timeout;
    int workers_count;
    int worker_queue_len;
    int workers_expelling_interval_ms;
    std::string cryptonode_rpc_address;
    int timer_poll_interval_ms;
    int log_trunc_to_size;
    std::vector<std::string> graftlet_dirs;
    int lru_timeout_ms;
    IPFilterOpts ipfilter;
    CommonOpts common;

    void check_asserts() const
    {
        assert(!http_address.empty());
        assert(!coap_address.empty());
        assert(0 < http_connection_timeout);
        assert(0 < upstream_request_timeout);
        assert(0 < workers_expelling_interval_ms);
        assert(0 < timer_poll_interval_ms);
        assert(0 < lru_timeout_ms);
        assert(ipfilter.requests_per_sec == 0 || 0 < ipfilter.window_size_sec);
    }
};

}
