
#pragma once

#include "supernode/server/config.h"

namespace graft::supernode {

struct Config : public server::Config
{
    std::string data_dir;             // base directory where supernode stake wallet and other supernodes wallets are located
    std::string stake_wallet_name;
    size_t stake_wallet_refresh_interval_ms;

    std::string watchonly_wallets_path;        // path to watch-only wallets (supernodes)
    bool testnet;                              // testnet flag


    Config(void);
};

}

