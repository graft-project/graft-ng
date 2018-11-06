#pragma once

#include "server.h"
#include "wallet_manager.h"

namespace graft {

/// Wallet service
class WalletServer: public GraftServer
{
    struct ConfigOptsEx : public ConfigOpts
    {
        // testnet flag
        bool testnet;
    };

public:
    WalletServer();
    ~WalletServer();
    WalletServer(const WalletServer&) = delete;
    WalletServer& operator = (const WalletServer&) = delete;

    /// Service entry point
    bool run(int argc, const char** argv);

protected:
    virtual void initMisc(ConfigOpts& configOpts) override;
    virtual bool initConfigOption(int argc, const char** argv, ConfigOpts& configOpts) override;
    virtual void initRouters() override;

private:    
    void initWalletManager();
    void startPeriodicTasks();
    void setHttpRouters(ConnectionManager& httpcm);
    void registerWalletRequests(ConnectionManager& httpcm);
    void flushWalletDiskCaches();

private:
    ConfigOptsEx m_configEx;
    std::unique_ptr<WalletManager> m_walletManager;
};

}//namespace graft
