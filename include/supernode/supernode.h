
#pragma once

#include "supernode/server.h"

namespace graft::snd {

class Supernode : public GraftServer
{
    struct ConfigOptsEx : public ConfigOpts
    {
        // data directory - base directory where supernode stake wallet and other supernodes wallets are located
        std::string data_dir;
        std::string stake_wallet_name;
        size_t stake_wallet_refresh_interval_ms;
        double stake_wallet_refresh_interval_random_factor;
        // runtime parameters.
        // path to watch-only wallets (supernodes)
        std::string watchonly_wallets_path;
        // testnet flag
        bool testnet;
    };

    void prepareDataDir();
    void startSupernodePeriodicTasks();
    void setHttpRouters(ConnectionManager& httpcm);
    void setCoapRouters(ConnectionManager& coapcm);
    void loadStakeWallets();

    ConfigOptsEx m_configEx;
protected:
    virtual void initMisc(ConfigOpts& configOpts) override;
    virtual bool initConfigOption(int argc, const char** argv, ConfigOpts& configOpts) override;
    virtual void initRouters() override;
public:
    bool run(int argc, const char** argv);

};

}

