#pragma once

#include "server.h"

namespace graft
{
namespace snd
{

class Supernode : public GraftServer
{
    struct ConfigOptsEx : public ConfigOpts
    {
        std::string stake_wallet_name;
        size_t stake_wallet_refresh_interval_ms;
        // runtime parameters.
        // path to watch-only wallets (supernodes)
        std::string watchonly_wallets_path;
    };

    void prepareDataDirAndSupernodes();
    void startSupernodePeriodicTasks();
    void setHttpRouters(HttpConnectionManager& httpcm);
    void setCoapRouters(CoapConnectionManager& coapcm);

    ConfigOptsEx m_configEx;
protected:
    virtual void initMisc(ConfigOpts& configOpts) override;
    virtual bool initConfigOption(int argc, const char** argv, ConfigOpts& configOpts) override;
    virtual void initConnectionManagers() override;
public:
    bool run(int argc, const char** argv)
    {
        for(bool run = true; run;)
        {
            run = false;
            if(!init(argc, argv, m_configEx)) return false;
            argc = 1;
            RunRes res = GraftServer::run();
            if(res == RunRes::SignalRestart) run = true;
        }
        return true;
    }
};

} //namespace snd
} //namespace graft
