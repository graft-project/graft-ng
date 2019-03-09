#include "rta/fullsupernodelist.h"

#include <wallet/api/wallet_manager.h>
#include <cryptonote_basic/cryptonote_basic_impl.h>
#include <cryptonote_protocol/blobdatatype.h>
#include <misc_log_ex.h>

#include <boost/multiprecision/cpp_int.hpp>
#include <boost/filesystem.hpp>

#include <algorithm>
#include <iostream>
#include <future>

#undef MONERO_DEFAULT_LOG_CATEGORY
#define MONERO_DEFAULT_LOG_CATEGORY "supernode.fullsupernodelist"

constexpr size_t STAKE_TRANSACTIONS_RECV_TIMEOUT_SECONDS    = 600;
constexpr size_t BLOCKCHAIN_BASED_LIST_RECV_TIMEOUT_SECONDS = 180;
constexpr size_t BLOCKCHAIN_BASED_LIST_DELAY_BLOCK_COUNT    = 10;

namespace fs = boost::filesystem;
using namespace boost::multiprecision;

using namespace std;

namespace {
    uint256_t hash_to_int256(const crypto::hash &hash)
    {
        cryptonote::blobdata str_val = std::string("0x") + epee::string_tools::pod_to_hex(hash);
        return uint256_t(str_val);
    }
    // this is WalletManager::findWallets immplenentation. only removed check for cache file.
    // TODO: fix this in GraftNetwork/wallet2_api lib
    std::vector<std::string> findWallets(const std::string &path)
    {
        std::vector<std::string> result;
        boost::filesystem::path work_dir(path);
        // return empty result if path doesn't exist
        if(!boost::filesystem::is_directory(path)){
            return result;
        }
        const boost::regex wallet_rx("(.*)\\.(keys)$"); // searching for <wallet_name>.keys files
        boost::filesystem::recursive_directory_iterator end_itr; // Default ctor yields past-the-end
        for (boost::filesystem::recursive_directory_iterator itr(path); itr != end_itr; ++itr) {
            // Skip if not a file
            if (!boost::filesystem::is_regular_file(itr->status()))
                continue;
            boost::smatch what;
            std::string filename = itr->path().filename().string();

            LOG_PRINT_L3("Checking filename: " << filename);

            bool matched = boost::regex_match(filename, what, wallet_rx);
            if (matched) {
                // if keys file found, checking if there's wallet file itself
                std::string wallet_file = (itr->path().parent_path() /= what[1].str()).string();
                LOG_PRINT_L3("Found wallet: " << wallet_file);
                result.push_back(wallet_file);

            }
        }
        return result;
    }

}

namespace graft {

namespace utils {

class ThreadPool
{
public:
    ThreadPool(size_t threads = 0);
    template <typename Callable>
    void enqueue(Callable job);
    void run();
    std::future<void> runAsync();

private:
    boost::asio::io_service m_ioservice;
    boost::thread_group m_threadpool;
    std::unique_ptr<boost::asio::io_service::work> m_work;
};

ThreadPool::ThreadPool(size_t threads)
{
    m_work = std::unique_ptr<boost::asio::io_service::work>{new boost::asio::io_service::work(m_ioservice)};
    if (threads == 0) {
        threads = boost::thread::hardware_concurrency();
    }
    for (int i = 0; i < threads; i++) {
        m_threadpool.create_thread(boost::bind(&boost::asio::io_service::run, &m_ioservice));
    }
}


template<typename Callable>
void ThreadPool::enqueue(Callable job)
{
    m_ioservice.dispatch(job);
}

void ThreadPool::run()
{
    m_work.reset();
    m_work.reset();
    while (!m_ioservice.stopped())
        m_ioservice.poll();
    m_threadpool.join_all();
    m_ioservice.stop();
}

std::future<void> ThreadPool::runAsync()
{
    return std::async(std::launch::async, [&]() {
       this->run();
    });
}

} // namespace utils;


#ifndef __cpp_inline_variables
constexpr int32_t FullSupernodeList::TIERS, FullSupernodeList::ITEMS_PER_TIER, FullSupernodeList::AUTH_SAMPLE_SIZE;
constexpr int64_t FullSupernodeList::AUTH_SAMPLE_HASH_HEIGHT, FullSupernodeList::ANNOUNCE_TTL_SECONDS;
#endif

FullSupernodeList::FullSupernodeList(const string &daemon_address, bool testnet)
    : m_daemon_address(daemon_address)
    , m_testnet(testnet)
    , m_rpc_client(daemon_address, "", "")
    , m_tp(new utils::ThreadPool())
    , m_blockchain_based_list_max_block_number()
    , m_last_recv_stake_txs(boost::date_time::not_a_date_time)
    , m_last_recv_blockchain_based_list(boost::date_time::not_a_date_time)
{
    m_refresh_counter = 0;
}

FullSupernodeList::~FullSupernodeList()
{
    boost::unique_lock<boost::shared_mutex> writerLock(m_access);
    m_list.clear();
}

bool FullSupernodeList::add(Supernode *item)
{
    return this->add(SupernodePtr{item});
}

bool FullSupernodeList::add(SupernodePtr item)
{
    if (exists(item->idKeyAsString())) {
        LOG_ERROR("item already exists: " << item->idKeyAsString());
        return false;
    }

    boost::unique_lock<boost::shared_mutex> writerLock(m_access);
    addImpl(item);
    return true;
}

void FullSupernodeList::addImpl(SupernodePtr item)
{
    m_list.insert(std::make_pair(item->idKeyAsString(), item));
    LOG_PRINT_L1("added supernode: " << item->idKeyAsString());
    LOG_PRINT_L1("list size: " << m_list.size());
}

size_t FullSupernodeList::loadFromDir(const string &base_dir)
{
//    vector<string> wallets = findWallets(base_dir);
//    size_t result = 0;
//    LOG_PRINT_L1("found wallets: " << wallets.size());
//    for (const auto &wallet_path : wallets) {
//        loadWallet(wallet_path);
//    }
    return this->size();
}



size_t FullSupernodeList::loadFromDirThreaded(const string &base_dir, size_t &found_wallets)
{
//    vector<string> wallets = findWallets(base_dir);
//    LOG_PRINT_L1("found wallets: " << wallets.size());
//    found_wallets = wallets.size();

//    utils::ThreadPool tp;

//    for (const auto &wallet_path : wallets) {
//        tp.enqueue(boost::bind<void>(&FullSupernodeList::loadWallet, this, wallet_path));
//    }

//    tp.run();
    return this->size();
}

bool FullSupernodeList::remove(const string &id)
{
    boost::unique_lock<boost::shared_mutex> writerLock(m_access);
    return m_list.erase(id) > 0;
}

size_t FullSupernodeList::size() const
{
    boost::shared_lock<boost::shared_mutex> readerLock(m_access);
    return m_list.size();
}

bool FullSupernodeList::exists(const string &id) const
{

    boost::shared_lock<boost::shared_mutex> readerLock(m_access);
    return m_list.find(id) != m_list.end();
}

//bool FullSupernodeList::update(const string &address, const vector<Supernode::SignedKeyImage> &key_images)
//{

//    boost::unique_lock<boost::shared_mutex> writerLock(m_access);
//    auto it = m_list.find(address);
//    if (it != m_list.end()) {
//        uint64_t height = 0;
//        return it->second->importKeyImages(key_images, height);
//    }
//    return false;
//}

SupernodePtr FullSupernodeList::get(const string &address) const
{
    boost::shared_lock<boost::shared_mutex> readerLock(m_access);
    auto it = m_list.find(address);
    if (it != m_list.end())
        return it->second;
    return SupernodePtr(nullptr);
}

bool FullSupernodeList::selectSupernodes(size_t items_count, const std::string& payment_id, const blockchain_based_list_tier& src_array, supernode_array& dst_array)
{
    size_t src_array_size = src_array.size();

    if (items_count > src_array_size)
        items_count = src_array_size;

    for (size_t i=0; i<src_array_size; i++)
    {
        auto supernode_it = m_list.find(src_array[i].supernode_public_id);

        if (supernode_it == m_list.end())
        {
            LOG_ERROR("attempt to select unknown supernode " << src_array[i].supernode_public_id);
            return false;
        }

        SupernodePtr supernode = supernode_it->second;    
        
        size_t random_value = m_rng();

        MDEBUG(".....select random value " << random_value << " items count is " << items_count << " with clamp to " << (src_array_size - i) << " items; result is " << (random_value % (src_array_size - i)));

        random_value %= src_array_size - i;

        if (random_value >= items_count)
            continue;

        MDEBUG(".....supernode " << src_array[i].supernode_public_id << " has been selected");

        dst_array.push_back(supernode);

        items_count--;
    }

    return true;
}

bool FullSupernodeList::buildAuthSample(uint64_t height, const std::string& payment_id, supernode_array &out)
{
    uint64_t blockchain_based_list_height = height - BLOCKCHAIN_BASED_LIST_DELAY_BLOCK_COUNT;

    MDEBUG("building auth sample for height " << height << "(blockchain_based_list_height=" << blockchain_based_list_height << ") and PaymentID '" << payment_id << "'");

    std::array<supernode_array, TIERS> tier_supernodes;
    {
        blockchain_based_list_ptr blockchain_based_list = findBlockchainBasedList(blockchain_based_list_height);

        if (!blockchain_based_list)
        {
            LOG_ERROR("unable to build auth sample for block height " << height << " (blockchain_based_list_height=" << blockchain_based_list_height << ") and PaymentID "
               << payment_id << ". Blockchain based list for this block is absent, latest block is " << getBlockchainBasedListMaxBlockNumber());
            return false;
        }

        boost::unique_lock<boost::shared_mutex> writerLock(m_access);

           //seed RNG
 
        std::seed_seq seed(reinterpret_cast<const unsigned char*>(payment_id.c_str()),
                           reinterpret_cast<const unsigned char*>(payment_id.c_str() + payment_id.size()));
 
        m_rng.seed(seed);
 
            //select supernodes for a full supernode list

        MDEBUG("use blockchain based list for height " << blockchain_based_list_height << " (" << blockchain_based_list.get() << ")");
        int t = 0;
        for (const blockchain_based_list_tier& l : *blockchain_based_list)
        {
          MDEBUG("...tier #" << t);
          int j=0;
          for (const blockchain_based_list_entry& e : l)
            MDEBUG(".....[" << j++ << "]=" << e.supernode_public_id);
          t++;
        }

        for (size_t i=0, tiers_count=blockchain_based_list->size(); i<TIERS && i<tiers_count; i++)
        {
            const blockchain_based_list_tier& src_array = (*blockchain_based_list)[i];
            supernode_array&                  dst_array = tier_supernodes[i];
            
            dst_array.reserve(AUTH_SAMPLE_SIZE);

            if (!selectSupernodes(AUTH_SAMPLE_SIZE, payment_id, src_array, dst_array))
            {
              LOG_ERROR("unable to select supernodes for auth sample");
              return false;
            }

            MDEBUG("..." << dst_array.size() << " supernodes has been selected for tier " << i << " from blockchain based list with " << src_array.size() << " supernodes");
        }
    }

    array<int, TIERS> select;
    select.fill(ITEMS_PER_TIER);
    // If we are short of the needed SNs on any tier try selecting additional SNs from the highest
    // tier with surplus SNs.  For example, if tier 2 is short by 1, look for a surplus first at
    // tier 4, then tier 3, then tier 1.
    for (int i = 0; i < TIERS; i++) {
        int deficit_i = select[i] - int(tier_supernodes[i].size());
        for (int j = TIERS-1; deficit_i > 0 && j >= 0; j--) {
            if (i == j) continue;
            int surplus_j = int(tier_supernodes[j].size()) - select[j];
            if (surplus_j > 0) {
                // Tier j has more SNs than needed, so select an extra SN from tier j to make up for
                // the deficiency at tier i.
                int transfer = std::min(deficit_i, surplus_j);
                select[i] -= transfer;
                select[j] += transfer;
                deficit_i -= transfer;
            }
        }
        // If we still have a deficit then no other tier has a surplus; we'll just have to work with
        // a smaller sample because there aren't enough SNs on the entire network.
        if (deficit_i > 0)
            select[i] -= deficit_i;
    }

    out.clear();
    out.reserve(ITEMS_PER_TIER * TIERS);
    auto out_it = back_inserter(out);
    for (int i = 0; i < TIERS; i++) {
        std::copy(tier_supernodes[i].begin(), tier_supernodes[i].begin() + select[i], out_it);
    }

    if (VLOG_IS_ON(2)) {
        std::string auth_sample_str, tier_sample_str;
        for (const auto &a : out) {
            auth_sample_str += a->idKeyAsString() + "\n";
        }
        for (size_t i = 0; i < select.size(); i++) {
            if (i > 0) tier_sample_str += ", ";
            tier_sample_str += std::to_string(select[i]) + " T"  + std::to_string(i+1);
        }
        MDEBUG("selected " << tier_sample_str << " supernodes of " << size() << " for auth sample");
        MTRACE("auth sample: \n" << auth_sample_str);
    }

    if (out.size() > AUTH_SAMPLE_SIZE)
      out.resize(AUTH_SAMPLE_SIZE);

    MDEBUG("..." << out.size() << " supernodes has been selected");

    return out.size() == AUTH_SAMPLE_SIZE;
}

bool FullSupernodeList::buildAuthSample(const string &payment_id, FullSupernodeList::supernode_array &out)
{
    return buildAuthSample(getBlockchainBasedListMaxBlockNumber(), payment_id, out);
}

vector<string> FullSupernodeList::items() const
{
    boost::shared_lock<boost::shared_mutex> readerLock(m_access);
    vector<string> result;
    result.reserve(m_list.size());
    for (auto const& it: m_list)
        result.push_back(it.first);

    return result;
}

bool FullSupernodeList::getBlockHash(uint64_t height, string &hash)
{
    bool result = m_rpc_client.get_block_hash(height, hash);
    return result;
}

std::future<void> FullSupernodeList::refreshAsync()
{
    m_refresh_counter = 0;
    auto worker = [&](const std::string &address) {
        SupernodePtr sn = this->get(address);
        if (sn) {
            sn->refresh();

            ++m_refresh_counter;
        }
    };

    for (const auto &address : this->items()) {
        m_tp->enqueue(boost::bind<void>(worker, address));
    }

    return m_tp->runAsync();
}

size_t FullSupernodeList::refreshedItems() const
{
    return m_refresh_counter;
}

void FullSupernodeList::updateStakeTransactions(const stake_transaction_array& stake_txs, const std::string& cryptonode_rpc_address, bool testnet)
{
    MDEBUG("update stake transactions");

    boost::unique_lock<boost::shared_mutex> writerLock(m_access);

      //clear supernode data

    for (const std::unordered_map<std::string, SupernodePtr>::value_type& sn_desc : m_list)
    {
        SupernodePtr sn = sn_desc.second;

        if (!sn)
            continue;

        sn->setStakeAmount(0);
        sn->setStakeTransactionBlockHeight(0);
        sn->setStakeTransactionUnlockTime(0);
    }

      //update supernodes

    for (const stake_transaction& tx : stake_txs)
    {
        auto it = m_list.find(tx.supernode_public_id);

        if (it != m_list.end())
        {
            SupernodePtr sn = it->second;

              //aggregate stake transaction fields for supernode 

            if (tx.amount)
            {
                if (sn->stakeAmount())
                {
                    sn->setStakeAmount(sn->stakeAmount() + tx.amount);

                      //find intersection of stake transaction intervals

                    uint64_t min_block_height    = sn->stakeTransactionBlockHeight(),
                             max_block_height    = min_block_height + sn->stakeTransactionUnlockTime(),
                             min_tx_block_height = tx.block_height,
                             max_tx_block_height = min_tx_block_height + tx.unlock_time;

                    if (min_tx_block_height > min_block_height)
                        min_block_height = min_tx_block_height;

                    if (max_tx_block_height < max_block_height)
                        max_block_height = max_tx_block_height;

                    if (max_block_height <= min_block_height)
                    {
                        sn->setStakeAmount(0);

                        max_block_height = min_block_height;
                    }

                    sn->setStakeTransactionBlockHeight(min_block_height);
                    sn->setStakeTransactionUnlockTime(max_block_height - min_block_height);
                }
                else
                {
                    sn->setStakeAmount(tx.amount);
                    sn->setStakeTransactionBlockHeight(tx.block_height);
                    sn->setStakeTransactionUnlockTime(tx.unlock_time);
                }
            }
        }
        else
        {
            SupernodePtr sn (Supernode::createFromStakeTransaction(tx, cryptonode_rpc_address, testnet));

            if (!sn)
            {
                LOG_ERROR("Cant create watch-only supernode wallet for id: " << tx.supernode_public_id);
                continue;
            }

            MINFO("About to add supernode to list [" << sn << "]: " << sn->idKeyAsString());

            addImpl(sn);
        }
    }

    m_last_recv_stake_txs = boost::posix_time::second_clock::local_time();
}

namespace
{

bool is_timeout_expired(const boost::posix_time::ptime& last_recv_time, size_t timeout_seconds)
{
    if (last_recv_time.is_not_a_date_time())
        return true;

    boost::posix_time::time_duration duration = boost::posix_time::second_clock::local_time() - last_recv_time;

    return duration > boost::posix_time::seconds(timeout_seconds);
}

}

void FullSupernodeList::synchronizeWithCryptonode(const char* network_address, const char* address)
{
    if (is_timeout_expired(m_last_recv_stake_txs, STAKE_TRANSACTIONS_RECV_TIMEOUT_SECONDS))
    {
        m_rpc_client.send_supernode_stake_txs(network_address, address);
    }

    if (is_timeout_expired(m_last_recv_blockchain_based_list, BLOCKCHAIN_BASED_LIST_RECV_TIMEOUT_SECONDS))
    {
        m_rpc_client.send_supernode_blockchain_based_list(network_address, address, m_blockchain_based_list_max_block_number);
    }
}

uint64_t FullSupernodeList::getBlockchainHeight() const
{
    uint64_t result = 0;
    bool ret = m_rpc_client.get_height(result);
    return ret ? result : 0;
}

void FullSupernodeList::setBlockchainBasedList(uint64_t block_number, const blockchain_based_list_ptr& list)
{
    boost::unique_lock<boost::shared_mutex> writerLock(m_access);

    MDEBUG("update blockchain based list for height " << block_number << " (" << list.get() << ")");
    int t = 0;
    for (const blockchain_based_list_tier& l : *list)
    {
      MDEBUG("...tier #" << t);
      int j=0;
      for (const blockchain_based_list_entry& e : l)
        MDEBUG(".....[" << j++ << "]=" << e.supernode_public_id);
      t++;
    }

    blockchain_based_list_map::iterator it = m_blockchain_based_lists.find(block_number);

    if (it != m_blockchain_based_lists.end())
    {
        MWARNING("Overriding blockchain based list for block " << block_number);
        it->second = list;
        return;
    }

    m_last_recv_blockchain_based_list = boost::posix_time::second_clock::local_time();

    m_blockchain_based_lists[block_number] = list;

    if (block_number > m_blockchain_based_list_max_block_number)
        m_blockchain_based_list_max_block_number = block_number;

      //flush cache - remove old blockchain based lists

    uint64_t oldest_block_number = m_blockchain_based_list_max_block_number - config::graft::SUPERNODE_HISTORY_SIZE;

    for (blockchain_based_list_map::iterator it=m_blockchain_based_lists.begin(); it!=m_blockchain_based_lists.end();)
      if (it->first < oldest_block_number) it = m_blockchain_based_lists.erase(it);
      else                                 ++it;
}

FullSupernodeList::blockchain_based_list_ptr FullSupernodeList::findBlockchainBasedList(uint64_t block_number) const
{
    boost::shared_lock<boost::shared_mutex> readerLock(m_access);

    blockchain_based_list_map::const_iterator it = m_blockchain_based_lists.find(block_number);

    if (it == m_blockchain_based_lists.end())
        return blockchain_based_list_ptr();

    return it->second;
}

uint64_t FullSupernodeList::getBlockchainBasedListMaxBlockNumber() const
{
    boost::shared_lock<boost::shared_mutex> readerLock(m_access);
    return m_blockchain_based_list_max_block_number;
}

std::ostream& operator<<(std::ostream& os, const std::vector<SupernodePtr> supernodes)
{
    for (size_t i = 0; i  < supernodes.size(); ++i) {
        os << supernodes[i]->idKeyAsString();
        if (i < supernodes.size() - 1)
            os << ", ";
    }
    return os;
}

}
