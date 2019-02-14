#include "walletnode/wallet_manager.h"
#include "lib/graft/inout.h"

#include "walletnode/requests/balance_request.h"
#include "walletnode/requests/create_account_request.h"
#include "walletnode/requests/restore_account_request.h"
#include "walletnode/requests/prepare_transfer_request.h"
#include "walletnode/requests/transaction_history_request.h"
#include "supernode/requestdefines.h"

#include "string_tools.h"

#include <mnemonics/electrum-words.h>

#include <wallet/graft_wallet.h>

using namespace graft;
using namespace graft::walletnode;
using namespace graft::walletnode::request;

namespace
{

const unsigned int WALLET_MEMORY_CACHE_TTL_SECONDS       = 10 * 60; //TODO: move to config
const unsigned int WALLET_TRANSACTIONS_QUEUE_SIZE        = 256; //TODO: move to config
const uint64_t     WALLET_DISK_CACHE_FLUSH_DELAY_SECONDS = 3600; //TODO: move to config
const char*        WALLETS_DIR_PREFIX                    = "wallets"; //TODO: move to config

/// Holder for lamba to be used inside FixedFunction
struct FixedFunctionWrapper
{
  struct IHolder
  {
    virtual void invoke() = 0;
  };

  template <class Fn> struct HolderImpl: public IHolder
  {
    Fn fn;

    HolderImpl(const Fn& in_fn) : fn(in_fn) {}

    void invoke() override { fn(); }
  };

  std::unique_ptr<IHolder> holder;

  template <class Fn> FixedFunctionWrapper(const Fn& fn) : holder(new HolderImpl<Fn>(fn)) {}

  void operator ()() const { holder->invoke(); }
};

/// URL parsing helper
struct URL
{
  std::string protocol, host, port, path, query;

  URL(const std::string& url_s)
  {
    const string prot_end("://");

    string::const_iterator prot_i = search(url_s.begin(), url_s.end(), prot_end.begin(), prot_end.end());

    protocol.reserve(distance(url_s.begin(), prot_i));

    transform(url_s.begin(), prot_i, back_inserter(protocol), ptr_fun<int,int>(tolower)); // protocol is icase

    if( prot_i == url_s.end() )
        return;

    advance(prot_i, prot_end.length());

    string::const_iterator port_i = find(prot_i, url_s.end(), ':');

    string::const_iterator path_i = find(prot_i, url_s.end(), '/');

    if (port_i == url_s.end())
    {
      host.reserve(distance(prot_i, path_i));

      port = "80"; //todo: make port based on protocol

      transform(prot_i, path_i, back_inserter(host), ptr_fun<int,int>(tolower)); // host is icase
    }
    else
    {
      host.reserve(distance(prot_i, port_i));

      transform(prot_i, port_i, back_inserter(host), ptr_fun<int,int>(tolower)); // host is icase

      port.assign(port_i + 1, path_i);
    }

    string::const_iterator query_i = find(path_i, url_s.end(), '?');

    path.assign(path_i, query_i);

    if( query_i != url_s.end() )
        ++query_i;

    query.assign(query_i, url_s.end());
  }
};

/// Handler for webhook callbacks
struct WebHookCallback
{
  Output result;

  WebHookCallback(const char* url_s)
  {
    URL url(url_s);

    result.proto        = url.protocol;
    result.host         = url.host;
    result.port         = url.port;
    result.path         = url.path;
    result.query_string = url.query;
  }

  void invoke(TaskManager& task_manager)
  {
    if (!task_manager.addPeriodicTask(*this, std::chrono::milliseconds(1)))
      LOG_PRINT_L1("Failed to invoke " << result.proto << "://" << result.host << ":" << result.port << result.path);
  }

  Status operator()(const Router::vars_t&, const graft::Input&, graft::Context& context, graft::Output& output)
  {
    if (context.local.hasKey(__FUNCTION__))
      return Status::Stop;

    LOG_PRINT_L2("Send response to " << result.proto << "://" << result.host << ":" << result.port << ". Body '" << result.body << "'");

    output = result;

    context.local[__FUNCTION__] = true;

    return Status::Forward;
  }
};

void invoke_error_http(const char* url, const char* error_text, TaskManager& task_manager)
{
  if (!*url)
    return;

  WebHookCallback callback(url);

  callback.result.body = "{'Error':'" + std::string(error_text) + "','Result':-1}";

  callback.invoke(task_manager);
}

void create_directories(const std::string& file_name)
{
  boost::filesystem::path path(file_name);
  boost::filesystem::path dir = path.parent_path();

  boost::filesystem::create_directories(dir);
}

}

using StrandX = tp::StrandImpl<tp::FixedFunction<void(), sizeof(GJPtr)>, tp::MPMCBoundedQueue>;

struct WalletManager::WalletHolder
{
  tools::GraftWallet wallet;
  StrandX            strand;

  WalletHolder(ThreadPoolX& thread_pool, bool testnet)
    : wallet(testnet)
    , strand(thread_pool, WALLET_TRANSACTIONS_QUEUE_SIZE)
  {
  }
};

WalletManager::WalletManager(TaskManager& task_manager, bool testnet)
  : m_testnet(testnet)
  , m_task_manager(task_manager)
{
  LOG_PRINT_L1("TestNet is " << testnet);
}

WalletManager::~WalletManager()
{
}

WalletManager::WalletPtr WalletManager::createWallet(Context& context)
{
  WalletPtr wallet(new WalletHolder(m_task_manager.getThreadPool(), m_testnet));

  wallet->wallet.init(context.global["cryptonode_rpc_address"]);

  return wallet;
}

void WalletManager::registerWallet(Context& context, const std::string& wallet_id, const WalletPtr& wallet)
{
  context.global.set(wallet_id, wallet, std::chrono::seconds(WALLET_MEMORY_CACHE_TTL_SECONDS)); //TODO: what about canceling expiration, when wallet has transactions in queue?
}

std::string WalletManager::getContextWalletId(const std::string& public_address)
{
  return "wallet_" + public_address;
}

template <class Fn>
void WalletManager::runAsyncForWallet
 (Context& context,
  const WalletId& public_address,
  const std::string& account_data,
  const std::string& password, 
  const Url& callback_url,
  const Fn& fn)
{
  std::string wallet_id = getContextWalletId(public_address);
  WalletPtr   wallet    = context.global.get<WalletPtr>(wallet_id, WalletPtr());

  if (!wallet)
  {
    wallet = createWallet(context);

    registerWallet(context, wallet_id, wallet);
  }

  wallet->strand.post(FixedFunctionWrapper([public_address, wallet, account_data, password, fn, callback_url, this]() {
    try
    {
      wallet->wallet.loadFromData(account_data, password);

      std::string cache_file_name = getWalletCacheFileName(public_address);

      wallet->wallet.load_cache(cache_file_name);

      wallet->wallet.refresh();

      WebHookCallback callback(callback_url.c_str());

      fn(wallet->wallet, callback.result);

      wallet->wallet.store_cache(cache_file_name);

      if (!callback_url.empty())
        callback.invoke(m_task_manager);
    }
    catch (std::exception& e)
    {
      LOG_PRINT_L1("Excepton " << e.what() << " during call " << __FUNCTION__);
      invoke_error_http(callback_url.c_str(), e.what(), m_task_manager);
    }
    catch (...)
    {
      LOG_PRINT_L1("Unhandled excepton during call " << __FUNCTION__);
      invoke_error_http(callback_url.c_str(), "unhandled exception", m_task_manager);
    }
  }));
}

template <class Fn>
void WalletManager::runAsync(Context& context, const Url& callback_url, const Fn& fn)
{
  m_task_manager.getThreadPool().post(FixedFunctionWrapper([fn, callback_url, this]() {
    try
    {    
      WebHookCallback callback(callback_url.c_str());

      fn(callback.result);

      if (!callback_url.empty())
        callback.invoke(m_task_manager);
    }
    catch (std::exception& e)
    {
      LOG_PRINT_L1("Excepton " << e.what() << " during call " << __FUNCTION__);
      invoke_error_http(callback_url.c_str(), e.what(), m_task_manager);
    }
    catch (...)
    {
      LOG_PRINT_L1("Unhandled excepton during call " << __FUNCTION__);
      invoke_error_http(callback_url.c_str(), "unhandled exception", m_task_manager);
    }
  }));
}

std::string WalletManager::getWalletCacheFileName(const WalletId& id)
{
  static const size_t WALLET_CACHE_FILE_NAME_PREFIX_SIZE = 2;

  if (id.size () < WALLET_CACHE_FILE_NAME_PREFIX_SIZE)
    return std::string(WALLETS_DIR_PREFIX) + "/" + id + ".cache";

  return std::string(WALLETS_DIR_PREFIX) + "/" + id.substr(0, WALLET_CACHE_FILE_NAME_PREFIX_SIZE) + "/" + id + ".cache";
}

void WalletManager::createAccount(Context& context, const std::string& password, const std::string& language, const Url& callback_url)
{
  GlobalContextMap* gcm = &context.global.getGcm();

  runAsync(context, callback_url, [gcm, callback_url, password, language, this](OutHttp& result) {
    Context context(*gcm);

    LOG_PRINT_L1("Create account (callback=" << callback_url << ")");

    WalletPtr wallet = createWallet(context);

    wallet->wallet.set_seed_language(language);
    
    crypto::secret_key secret_key = wallet->wallet.generateFromData(password);
    std::string account_data = wallet->wallet.getAccountData(password);
    const cryptonote::account_base& account = wallet->wallet.get_account();
    std::string public_address = account.get_public_address_str(m_testnet);
    std::string view_key (&account.get_keys().m_view_secret_key.data[0], sizeof(account.get_keys().m_view_secret_key.data));
    std::string seed;

    if (!wallet->wallet.get_seed(seed))
      throw std::runtime_error("Can't get seed for new wallet");

    LOG_PRINT_L1("Seed is '" << seed << "'");
    LOG_PRINT_L1("AccountData is '" << account_data << "'");

    std::string cache_file_name = getWalletCacheFileName(public_address);

    create_directories(cache_file_name);

    wallet->wallet.store_cache(cache_file_name);

    std::string wallet_id = getContextWalletId(public_address);

    registerWallet(context, wallet_id, wallet);

    WalletCreateAccountCallbackRequest out;

    out.Result   = 0;
    out.Address  = public_address;
    out.ViewKey  = view_key;
    out.Account  = account_data;
    out.Seed     = seed;
    out.WalletId = public_address;

    result.load(out);

    LOG_PRINT_L1("Wallet '" << public_address << "' has been successfully loaded (callback=" << callback_url << ")");
  });
}

void WalletManager::restoreAccount(Context& context, const std::string& password, const std::string& seed, const Url& callback_url)
{
  GlobalContextMap* gcm = &context.global.getGcm();

  runAsync(context, callback_url, [gcm, callback_url, password, seed, this](OutHttp& result) {
    Context context(*gcm);

    LOG_PRINT_L1("Restore account (callback=" << callback_url << ")");

    WalletPtr wallet = createWallet(context);

    crypto::secret_key recovery_key;
    std::string old_language = "English";

    LOG_PRINT_L1("Restore seed is '" << seed << "'");

    if (!crypto::ElectrumWords::words_to_bytes(seed, recovery_key, old_language))
    {
      LOG_PRINT_L1("Restore account failed due to electrum-style word list verification");
      //TODO: error reporting
      return;
    }

    crypto::secret_key secret_key = wallet->wallet.generateFromData(password, recovery_key, true);
    const cryptonote::account_base& account = wallet->wallet.get_account();
    std::string public_address = account.get_public_address_str(m_testnet);
    std::string account_data = wallet->wallet.getAccountData(password);    
    std::string view_key (&account.get_keys().m_view_secret_key.data[0], sizeof(account.get_keys().m_view_secret_key.data));
    std::string seed;

    if (!wallet->wallet.get_seed(seed))
      throw std::runtime_error("Can't get seed for new wallet");

    std::string cache_file_name = getWalletCacheFileName(public_address);

    create_directories(cache_file_name);

    wallet->wallet.store_cache(cache_file_name);

    std::string wallet_id = getContextWalletId(public_address);

    registerWallet(context, wallet_id, wallet);    

    WalletRestoreAccountCallbackRequest out;

    out.Result   = 0;
    out.Address  = public_address;
    out.ViewKey  = view_key;
    out.Account  = account_data;
    out.Seed     = seed;
    out.WalletId = public_address;

    result.load(out);

    LOG_PRINT_L1("Wallet '" << public_address << "' has been successfully restored (callback=" << callback_url << ")");
  });
}

void WalletManager::requestBalance(Context& context, const WalletId& wallet_id, const std::string& account_data, const std::string& password, const Url& callback_url)
{
  runAsyncForWallet(context, wallet_id, account_data, password, callback_url, [this, wallet_id, callback_url](tools::GraftWallet& wallet, OutHttp& result) {
    LOG_PRINT_L1("Request balance for wallet '" << wallet_id << "'(callback=" << callback_url << ")");

    WalletBalanceCallbackRequest out;

    out.Result          = 0;
    out.Balance         = std::to_string(wallet.balance());
    out.UnlockedBalance = std::to_string(wallet.unlocked_balance());

    result.load(out);
  });
}

void WalletManager::prepareTransfer(Context& context, const WalletId& wallet_id, const std::string& account_data, const std::string& password, const TransferDestinationArray& in_destinations, const Url& callback_url)
{
  std::vector<cryptonote::tx_destination_entry> destinations;

  destinations.reserve(in_destinations.size());

  for (const TransferDestination& dest : in_destinations)
  {
      cryptonote::account_public_address address;

      memset(&address.m_view_public_key.data[0], 0, sizeof(address.m_view_public_key.data));

      assert(dest.address.size() >= sizeof(address.m_spend_public_key.data));

      memcpy(&address.m_spend_public_key.data[0], dest.address.c_str(), sizeof(address.m_spend_public_key.data));

      destinations.push_back(cryptonote::tx_destination_entry(dest.amount, address));
  }

  runAsyncForWallet(context, wallet_id, account_data, password, callback_url, [this, wallet_id, destinations, callback_url](tools::GraftWallet& wallet, OutHttp& result) {
    LOG_PRINT_L1("Prepare transfer for wallet '" << wallet_id << "'(callback=" << callback_url << ")");

    const size_t fake_outs_count = 0;
    const uint64_t unlock_time = 0;
    const uint32_t priority = 0;
    const std::vector<uint8_t> extra;
    const bool trusted_daemon = true;

    std::vector<tools::GraftWallet::pending_tx> transactions = wallet.create_transactions(destinations, fake_outs_count, unlock_time, priority, extra, trusted_daemon);

    std::vector<std::string> serialized_transactions;

    serialized_transactions.reserve(transactions.size());

    uint64_t total_fee = 0;

    for (const tools::GraftWallet::pending_tx& transaction : transactions)
    {
        std::string serialized_transaction = epee::string_tools::buff_to_hex_nodelimer(cryptonote::tx_to_blob(transaction.tx));

        serialized_transactions.push_back(serialized_transaction);

        total_fee += transaction.fee;
    }

    WalletPrepareTransferCallbackRequest out;

    out.Result       = 0;
    out.Fee          = std::to_string(total_fee);
    out.Transactions = std::move(serialized_transactions);

    result.load(out);
  });
}

void WalletManager::requestTransactionHistory
 (Context& context,
  const WalletId& wallet_id,
  const std::string& account_data,
  const std::string& password,
  const Url& callback_url)
{
  runAsyncForWallet(context, wallet_id, account_data, password, callback_url, [this, wallet_id, callback_url](tools::GraftWallet& wallet, OutHttp& result) {
    LOG_PRINT_L1("Request transaction history for wallet '" << wallet_id << "'(callback=" << callback_url << ")");

    TransactionHistory transaction_history;
    std::vector<TransactionInfo>& transactions = transaction_history.Transactions;

    uint64_t wallet_height = wallet.get_blockchain_current_height();
    uint64_t min_height = 0, max_height = (uint64_t)-1;

    std::list<std::pair<crypto::hash, tools::wallet2::payment_details>> in_payments;

    wallet.get_payments(in_payments, min_height, max_height);

    for (std::list<std::pair<crypto::hash, tools::wallet2::payment_details>>::const_iterator it = in_payments.begin(); it != in_payments.end(); ++it)
    {
      const tools::wallet2::payment_details& details = it->second;
      std::string payment_id = epee::string_tools::pod_to_hex(it->first);

      if (payment_id.substr(16).find_first_not_of('0') == std::string::npos)
        payment_id = payment_id.substr(0,16);

      TransactionInfo info;

      info.PaymentId     = payment_id;
      info.Amount        = details.m_amount;
      info.DirectionOut = false;
      info.Hash          = epee::string_tools::pod_to_hex(details.m_tx_hash);
      info.BlockHeight   = details.m_block_height;
      info.Timestamp     = details.m_timestamp;
      info.Confirmations = wallet_height - details.m_block_height;
      info.UnlockTime    = details.m_unlock_time;

      transactions.emplace_back(std::move(info));
    }

    std::list<std::pair<crypto::hash, tools::wallet2::confirmed_transfer_details>> out_payments;

    wallet.get_payments_out(out_payments, min_height, max_height);

    for (std::list<std::pair<crypto::hash, tools::wallet2::confirmed_transfer_details>>::const_iterator it = out_payments.begin(); it != out_payments.end(); ++it)
    {    
      const crypto::hash &hash = it->first;
      const tools::wallet2::confirmed_transfer_details &details = it->second;
      
      uint64_t change = details.m_change == (uint64_t)-1 ? 0 : details.m_change; // change may not be known
      uint64_t fee = details.m_amount_in - details.m_amount_out;
      
      std::string payment_id = epee::string_tools::pod_to_hex(it->second.m_payment_id);

      if (payment_id.substr(16).find_first_not_of('0') == std::string::npos)
          payment_id = payment_id.substr(0,16);

      TransactionInfo info;

      info.PaymentId     = payment_id;
      info.Amount        = details.m_amount_in - change - fee;
      info.Fee           = fee;
      info.DirectionOut = true;
      info.Hash          = epee::string_tools::pod_to_hex(hash);
      info.BlockHeight   = details.m_block_height;
      info.Timestamp     = details.m_timestamp;
      info.Confirmations = wallet_height - details.m_block_height;

      for (const auto &d: details.m_dests) {
          Transfer transfer;

          transfer.Amount  = d.amount;
          transfer.Address = get_account_address_as_str(wallet.testnet(), d.addr);

          info.Transfers.emplace_back(std::move(transfer));
      }

      transactions.emplace_back(std::move(info));
    }

    std::list<std::pair<crypto::hash, tools::wallet2::unconfirmed_transfer_details>> upayments_out;

    wallet.get_unconfirmed_payments_out(upayments_out);

    for (std::list<std::pair<crypto::hash, tools::wallet2::unconfirmed_transfer_details>>::const_iterator it = upayments_out.begin(); it != upayments_out.end(); ++it)
    {
      const tools::wallet2::unconfirmed_transfer_details &details = it->second;
      const crypto::hash &hash = it->first;

      uint64_t amount = details.m_amount_in;
      uint64_t fee = amount - details.m_amount_out;

      std::string payment_id = epee::string_tools::pod_to_hex(it->second.m_payment_id);

      if (payment_id.substr(16).find_first_not_of('0') == std::string::npos)
          payment_id = payment_id.substr(0,16);

      bool is_failed = details.m_state == tools::wallet2::unconfirmed_transfer_details::failed;

      TransactionInfo info;

      info.PaymentId     = payment_id;
      info.Amount        = amount - details.m_change;
      info.Fee           = fee;
      info.DirectionOut = true;
      info.Failed        = is_failed;
      info.Pending       = true;
      info.Hash          = epee::string_tools::pod_to_hex(hash);
      info.Timestamp     = details.m_timestamp;
      info.Confirmations = 0;

      transactions.emplace_back(std::move(info));
    }

    std::list<std::pair<crypto::hash, tools::wallet2::payment_details>> upayments;

    wallet.get_unconfirmed_payments(upayments);

    for (std::list<std::pair<crypto::hash, tools::wallet2::payment_details>>::const_iterator it = upayments.begin(); it != upayments.end(); ++it)
    {
      const tools::wallet2::payment_details &details = it->second;
      std::string payment_id = epee::string_tools::pod_to_hex(it->first);

      if (payment_id.substr(16).find_first_not_of('0') == std::string::npos)
          payment_id = payment_id.substr(0,16);

      TransactionInfo info;

      info.PaymentId     = payment_id;
      info.Amount        = details.m_amount;
      info.DirectionOut = false;
      info.Hash          = epee::string_tools::pod_to_hex(details.m_tx_hash);
      info.BlockHeight   = details.m_block_height;
      info.Pending       = true;
      info.Timestamp     = details.m_timestamp;
      info.Confirmations = 0;

      transactions.emplace_back(std::move(info));
    }

    WalletTransactionHistoryCallbackRequest out;

    out.Result  = 0;
    out.History = std::move(transaction_history);

    result.load(out);
  });
}

namespace
{

std::vector<std::string> getAllExpiredFiles(const char* dir, const char* name_re)
{
  time_t now;

  time(&now);

  const std::string target_path(dir);
  const boost::regex my_filter(name_re);

  std::vector<std::string> result;

  boost::filesystem::directory_iterator end_itr;

  for (boost::filesystem::directory_iterator it(target_path); it != end_itr; ++it)
  {
    if (!boost::filesystem::is_regular_file(it->status()))
      continue;

    boost::smatch what;
    std::string file_name = it->path().filename().native();

    if (!boost::regex_match(file_name, what, my_filter))
      continue;

    std::time_t modification_time = last_write_time(it->path());
    double seconds_delta = difftime(now, modification_time);

    if (seconds_delta < WALLET_DISK_CACHE_FLUSH_DELAY_SECONDS)
      continue;

    result.push_back(file_name);
  }

  return result;
}

}

void WalletManager::flushDiskCaches()
{
  LOG_PRINT_L1("Flush disk caches");

  std::vector<std::string> cache_files = getAllExpiredFiles(".", ".*\\.cache");

  for (const std::string& cache_file_name : cache_files)
  {
    LOG_PRINT_L1("  remove cache file " << cache_file_name);

    boost::filesystem::remove(cache_file_name);
  }
}
