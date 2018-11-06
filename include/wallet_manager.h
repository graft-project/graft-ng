#pragma once

#include "context.h"
#include "task.h"
#include "thread_pool/strand.hpp"

#include <atomic>
#include <string>
#include <memory>
#include <vector>

namespace tools
{

class GraftWallet;

}

namespace graft
{

/// Wallet manager
class WalletManager
{
public:
    typedef std::string WalletId;
    typedef std::string Url;

    struct TransferDestination
    {
      std::string address;
      uint64_t    amount;

      TransferDestination(const std::string& in_address, uint64_t in_amount)
        : address(in_address), amount(in_amount) {}
    };

    typedef std::vector<TransferDestination> TransferDestinationArray;

    // Constructors / destructor
    WalletManager(TaskManager& task_manager, bool testnet = false);
    ~WalletManager();
    WalletManager(const WalletManager&) = delete;
    WalletManager& operator = (const WalletManager&) = delete;

    /// Create account
    void createAccount(Context&, const std::string& password, const std::string& language, const Url& callback_url = Url());

    /// Restore account
    void restoreAccount(Context&, const std::string& password, const std::string& seed, const Url& callback_url = Url());

    /// Request balance
    void requestBalance(Context&, const WalletId&, const std::string& account_data, const std::string& password, const Url& callback_url = Url());

    /// Prepare transfer
    void prepareTransfer(Context&, const WalletId&, const std::string& account_data, const std::string& password, const TransferDestinationArray& destinations, const Url& callback_url = Url());

    /// Flush disk caches
    void flushDiskCaches();

private:
    struct WalletHolder;
    typedef std::shared_ptr<WalletHolder> WalletPtr;

private:
    // Creates new wallet
    WalletPtr createWallet(Context&);

    // Returns wallet id in context
    static std::string getContextWalletId(const std::string& public_address);

    // Register wallet in global context
    void registerWallet(Context&, const WalletId&, const WalletPtr&);

    // Executes asynchronously for specific wallet
    template <class Fn> void runAsyncForWallet(Context& context, const WalletId& wallet_id, const std::string& account_data,
        const std::string& password, const Url& callback_url, const Fn& fn);
    template <class Fn> void runAsync(Context& context, const Url& callback_url, const Fn& fn);

    // Generate wallet cache file name from ID
    static std::string getWalletCacheFileName(const WalletId&);

private:
    bool         m_testnet;
    TaskManager& m_task_manager;
};

}//namespace graft
