#pragma once

#include "supernode/server.h"
#include "walletnode/wallet_manager.h"

namespace graft {

namespace walletnode {

/// Wallet service
class WalletServer: public GraftServer
{
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

    ConfigOpts m_configOpts;
    std::unique_ptr<WalletManager> m_walletManager;
};

}//namespace walletnode
}//namespace graft
