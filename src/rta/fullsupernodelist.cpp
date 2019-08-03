#include "rta/fullsupernodelist.h"

#include <wallet/api/wallet_manager.h>
#include <cryptonote_basic/cryptonote_basic_impl.h>
#include <cryptonote_basic/blobdatatype.h>
#include <misc_log_ex.h>

#include <boost/multiprecision/cpp_int.hpp>
#include <boost/filesystem.hpp>

#include <algorithm>
#include <iostream>
#include <future>

#undef MONERO_DEFAULT_LOG_CATEGORY
#define MONERO_DEFAULT_LOG_CATEGORY "supernode.fullsupernodelist"

constexpr size_t STAKES_RECV_TIMEOUT_SECONDS                = 600;
constexpr size_t BLOCKCHAIN_BASED_LIST_RECV_TIMEOUT_SECONDS = 180;
constexpr size_t REPEATED_REQUEST_DELAY_SECONDS             = 10;

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
    , m_stakes_max_block_number()
    , m_next_recv_stakes(boost::date_time::not_a_date_time)
    , m_next_recv_blockchain_based_list(boost::date_time::not_a_date_time)
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

bool FullSupernodeList::selectSupernodes(size_t items_count, const blockchain_based_list_tier& src_array, supernode_array& dst_array)
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

uint64_t FullSupernodeList::getBlockchainBasedListForAuthSample(uint64_t block_number, blockchain_based_list& list) const
{
    boost::shared_lock<boost::shared_mutex> readerLock(m_access);

    uint64_t blockchain_based_list_height = block_number - BLOCKCHAIN_BASED_LIST_DELAY_BLOCK_COUNT;

    blockchain_based_list_map::const_iterator it = m_blockchain_based_lists.find(block_number);

    if (it == m_blockchain_based_lists.end())
        return 0;

    blockchain_based_list     result;
    blockchain_based_list_ptr bbl = it->second;

    result.block_hash = bbl->block_hash;

    for (blockchain_based_list_tier& src : bbl->tiers)
    {
        blockchain_based_list_tier dst;

        std::copy_if(src.begin(), src.end(), std::back_inserter(dst), [this](const blockchain_based_list_entry& entry)->bool
        {
            auto it = m_list.find(entry.supernode_public_id);

            if (it == m_list.end())
                return false;

            const SupernodePtr& sn              = it->second;
            uint64_t            last_update_age = static_cast<unsigned>(std::time(nullptr)) - sn->lastUpdateTime();

            if (FullSupernodeList::ANNOUNCE_TTL_SECONDS < last_update_age)
                return false;

            return true;
        });

        result.tiers.emplace_back(std::move(dst));
    }

    list = std::move(result);

    return blockchain_based_list_height;
}

bool FullSupernodeList::buildSample(const blockchain_based_list& bbl, size_t sample_size, const char* prefix, supernode_array &out)
{
    std::array<supernode_array, TIERS> tier_supernodes;

        //select supernodes for a full supernode list

    for (size_t i=0, tiers_count=bbl.tiers.size(); i<TIERS && i<tiers_count; i++)
    {
        const blockchain_based_list_tier& src_array = bbl.tiers[i];
        supernode_array&                  dst_array = tier_supernodes[i];
        
        dst_array.reserve(sample_size);

        if (!selectSupernodes(sample_size, src_array, dst_array))
        {
          LOG_ERROR("unable to select supernodes for " << prefix << " sample");
          return false;
        }

        MDEBUG("..." << dst_array.size() << " supernodes has been selected for tier " << (i + 1) << " from blockchain based list with " << src_array.size() << " supernodes");
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
        std::string sample_str, tier_sample_str;
        for (const auto &a : out) {
            sample_str += a->idKeyAsString() + "\n";
        }
        for (size_t i = 0; i < select.size(); i++) {
            if (i > 0) tier_sample_str += ", ";
            tier_sample_str += std::to_string(select[i]) + " T"  + std::to_string(i+1);
        }
        MDEBUG("selected " << tier_sample_str << " supernodes of " << size() << " for " << prefix << " sample");
        MTRACE(prefix << " sample: \n" << sample_str);
    }

    if (out.size() > sample_size)
      out.resize(sample_size);

    MDEBUG("..." << out.size() << " supernodes has been selected");

    return out.size() == sample_size;
}

bool FullSupernodeList::buildAuthSample(uint64_t height, const std::string& payment_id, supernode_array &out, uint64_t &out_auth_block_number)
{
    blockchain_based_list bbl;

    out_auth_block_number = getBlockchainBasedListForAuthSample(height, bbl);

    if (!out_auth_block_number)
    {
        LOG_ERROR("unable to build auth sample for block height " << height << " (blockchain_based_list_height=" << (height - BLOCKCHAIN_BASED_LIST_DELAY_BLOCK_COUNT) << ") and PaymentID "
           << payment_id << ". Blockchain based list for this block is absent, latest block is " << getBlockchainBasedListMaxBlockNumber());
        return false;
    }

    MDEBUG("building auth sample for height " << height << " (blockchain_based_list_height=" << out_auth_block_number << ") and PaymentID '" << payment_id << "'");

    boost::unique_lock<boost::shared_mutex> writerLock(m_access);

       //seed RNG

    std::seed_seq seed(reinterpret_cast<const unsigned char*>(payment_id.c_str()),
                       reinterpret_cast<const unsigned char*>(payment_id.c_str() + payment_id.size()));

    m_rng.seed(seed);

      //build sample

    return buildSample(bbl, AUTH_SAMPLE_SIZE, "auth", out);
}

bool FullSupernodeList::buildAuthSample(const string &payment_id, FullSupernodeList::supernode_array &out, uint64_t &out_auth_block_number)
{
    return buildAuthSample(getBlockchainBasedListMaxBlockNumber(), payment_id, out, out_auth_block_number);
}

bool FullSupernodeList::buildDisqualificationSamples(uint64_t height, supernode_array &out_disqualification_sample, supernode_array &out_disqualification_candidates, uint64_t &out_auth_block_number)
{
    blockchain_based_list bbl;

    out_auth_block_number = getBlockchainBasedListForAuthSample(height, bbl);

    if (!out_auth_block_number)
    {
        LOG_ERROR("unable to build disqualification samples for block height " << height << " (blockchain_based_list_height=" << (height - BLOCKCHAIN_BASED_LIST_DELAY_BLOCK_COUNT) << ") "
          ". Blockchain based list for this block is absent, latest block is " << getBlockchainBasedListMaxBlockNumber());
        return false;
    }

    MDEBUG("building disqualification samples for height " << height << " (blockchain_based_list_height=" << out_auth_block_number << ") and hash=" << bbl.block_hash);

    boost::unique_lock<boost::shared_mutex> writerLock(m_access);

       //seed RNG

    std::seed_seq seed(reinterpret_cast<const unsigned char*>(bbl.block_hash.c_str()),
                       reinterpret_cast<const unsigned char*>(bbl.block_hash.c_str() + bbl.block_hash.size()));

    m_rng.seed(seed);

      //build sample

    return buildSample(bbl, DISQUALIFICATION_SAMPLE_SIZE, "disqualification", out_disqualification_sample) &&
           buildSample(bbl, DISQUALIFICATION_CANDIDATES_SIZE, "disqualification candidates", out_disqualification_candidates);
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

void FullSupernodeList::updateStakes(uint64_t block_number, const supernode_stake_array& stakes, const std::string& cryptonode_rpc_address, bool testnet)
{
    MDEBUG("update stakes");

    boost::unique_lock<boost::shared_mutex> writerLock(m_access);

    if (block_number <= m_stakes_max_block_number)
    {
      MDEBUG("stakes for block #" << block_number << " have already been received (last stakes have been received for block #" << m_stakes_max_block_number << ")");
      return;
    }

      //clear supernode data

    for (const std::unordered_map<std::string, SupernodePtr>::value_type& sn_desc : m_list)
    {
        SupernodePtr sn = sn_desc.second;

        if (!sn)
            continue;

        sn->setStake(0, 0, 0);
    }

      //update supernodes

    for (const supernode_stake& stake : stakes)
    {
        auto it = m_list.find(stake.supernode_public_id);

        if (it == m_list.end())
        {
            SupernodePtr sn (Supernode::createFromStake(stake, cryptonode_rpc_address, testnet));

            if (!sn)
            {
                LOG_ERROR("Cant create watch-only supernode wallet for id: " << stake.supernode_public_id);
                continue;
            }

            MINFO("About to add supernode to list [" << sn << "]: " << sn->idKeyAsString());

            addImpl(sn);

            continue;
        }

          //update stake

        SupernodePtr sn = it->second;

        sn->setStake(stake.amount, stake.block_height, stake.unlock_time);
        sn->setWalletAddress(stake.supernode_public_address);
    }

    m_stakes_max_block_number = block_number;
    m_next_recv_stakes = boost::posix_time::second_clock::local_time() + boost::posix_time::seconds(STAKES_RECV_TIMEOUT_SECONDS);
}

namespace
{

bool check_timeout_expired(boost::posix_time::ptime& next_recv_time)
{
    boost::posix_time::ptime now = boost::posix_time::second_clock::local_time();

    if (!next_recv_time.is_special() && next_recv_time > now)
      return false;

    next_recv_time = now + boost::posix_time::seconds(REPEATED_REQUEST_DELAY_SECONDS);

    return true;
}

}

void FullSupernodeList::synchronizeWithCryptonode(const char* network_address, const char* address)
{
    if (check_timeout_expired(m_next_recv_stakes))
    {
        m_rpc_client.send_supernode_stakes(network_address, address);
    }

    if (check_timeout_expired(m_next_recv_blockchain_based_list))
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

    blockchain_based_list_map::iterator it = m_blockchain_based_lists.find(block_number);

    if (it != m_blockchain_based_lists.end())
    {
        MWARNING("Overriding blockchain based list for block " << block_number);
        it->second = list;
        return;
    }

    m_next_recv_blockchain_based_list = boost::posix_time::second_clock::local_time() + boost::posix_time::seconds(BLOCKCHAIN_BASED_LIST_RECV_TIMEOUT_SECONDS);

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

bool FullSupernodeList::hasBlockchainBasedList(uint64_t block_number) const
{
    return findBlockchainBasedList(block_number) != blockchain_based_list_ptr();
}

size_t FullSupernodeList::getSupernodeBlockchainBasedListTier(const std::string& supernode_public_id, uint64_t block_number) const
{
    FullSupernodeList::blockchain_based_list_ptr list = findBlockchainBasedList(block_number);

    if (!list)
        return false;

    size_t tier_number = 1;

    for (const blockchain_based_list_tier& tier : list->tiers)
    {
        for (const blockchain_based_list_entry& sn : tier)
            if (sn.supernode_public_id == supernode_public_id)
                return tier_number;

        tier_number++;
    }

    return 0;
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
