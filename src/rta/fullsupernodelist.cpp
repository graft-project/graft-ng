#include "fullsupernodelist.h"

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


const uint8_t FullSupernodeList::AUTH_SAMPLE_SIZE;
const size_t FullSupernodeList::ITEMS_PER_TIER;
const uint64_t FullSupernodeList::AUTH_SAMPLE_HASH_HEIGHT;
const uint64_t FullSupernodeList::ANNOUNCE_TTL_SECONDS;

FullSupernodeList::FullSupernodeList(const string &daemon_address, bool testnet)
    : m_daemon_address(daemon_address)
    , m_testnet(testnet)
    , m_rpc_client(daemon_address, "", "")
    , m_tp(new utils::ThreadPool())
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
    if (exists(item->walletAddress())) {
        LOG_ERROR("item already exists: " << item->walletAddress());
        return false;
    }

    boost::unique_lock<boost::shared_mutex> writerLock(m_access);
    m_list.insert(std::make_pair(item->walletAddress(), item));
    LOG_PRINT_L1("added supernode: " << item->walletAddress());
    LOG_PRINT_L1("list size: " << m_list.size());
    return true;
}

size_t FullSupernodeList::loadFromDir(const string &base_dir)
{
    vector<string> wallets = findWallets(base_dir);
    size_t result = 0;
    LOG_PRINT_L1("found wallets: " << wallets.size());
    for (const auto &wallet_path : wallets) {
        loadWallet(wallet_path);
    }
    return this->size();
}



size_t FullSupernodeList::loadFromDirThreaded(const string &base_dir, size_t &found_wallets)
{
    vector<string> wallets = findWallets(base_dir);
    LOG_PRINT_L1("found wallets: " << wallets.size());
    found_wallets = wallets.size();

    utils::ThreadPool tp;

    for (const auto &wallet_path : wallets) {
        tp.enqueue(boost::bind<void>(&FullSupernodeList::loadWallet, this, wallet_path));
    }

    tp.run();
    return this->size();
}

bool FullSupernodeList::remove(const string &address)
{
    boost::unique_lock<boost::shared_mutex> readerLock(m_access);
    m_list.erase(address) > 0;
}

size_t FullSupernodeList::size() const
{
    boost::shared_lock<boost::shared_mutex> readerLock(m_access);
    return m_list.size();
}

bool FullSupernodeList::exists(const string &address) const
{

    boost::shared_lock<boost::shared_mutex> readerLock(m_access);
    return m_list.find(address) != m_list.end();
}

bool FullSupernodeList::update(const string &address, const vector<Supernode::SignedKeyImage> &key_images)
{

    boost::unique_lock<boost::shared_mutex> writerLock(m_access);
    auto it = m_list.find(address);
    if (it != m_list.end()) {
        uint64_t height = 0;
        return it->second->importKeyImages(key_images, height);
    }
    return false;
}

SupernodePtr FullSupernodeList::get(const string &address) const
{
    boost::shared_lock<boost::shared_mutex> readerLock(m_access);
    auto it = m_list.find(address);
    if (it != m_list.end())
        return it->second;
    return SupernodePtr(nullptr);
}

bool FullSupernodeList::buildAuthSample(uint64_t height, vector<SupernodePtr> &out)
{
    crypto::hash block_hash;
    string  block_hash_str;

    MDEBUG("building auth sample for height: " << height);

    if (!getBlockHash(height - AUTH_SAMPLE_HASH_HEIGHT, block_hash_str)) {
        LOG_ERROR("getBlockHash error");
        return false;
    }

    epee::string_tools::hex_to_pod(block_hash_str, block_hash);
    vector<SupernodePtr> tier_supernodes;
    out.clear();

    auto out_it = back_inserter(out);


    auto build_tier_sample = [&](uint64_t tier_min, uint64_t tier_max, size_t max_items) -> size_t {
        if (max_items == 0)
            return 0;
        selectTierSupernodes(block_hash, tier_min, tier_max, tier_supernodes, out, max_items);
        copy(tier_supernodes.begin(), tier_supernodes.end(), out_it);
        size_t items_selected =  tier_supernodes.size();
        tier_supernodes.clear();
        return items_selected;
    };

    // infinite loop protection
    size_t tries_left = AUTH_SAMPLE_SIZE * AUTH_SAMPLE_SIZE; // not sure if it needs to be N^2

    while (out.size() < AUTH_SAMPLE_SIZE && tries_left--) {
        build_tier_sample(Supernode::TIER1_STAKE_AMOUNT, Supernode::TIER2_STAKE_AMOUNT, std::min(ITEMS_PER_TIER, AUTH_SAMPLE_SIZE - out.size()));
        build_tier_sample(Supernode::TIER2_STAKE_AMOUNT, Supernode::TIER3_STAKE_AMOUNT, std::min(ITEMS_PER_TIER, AUTH_SAMPLE_SIZE - out.size()));
        build_tier_sample(Supernode::TIER3_STAKE_AMOUNT, Supernode::TIER4_STAKE_AMOUNT, std::min(ITEMS_PER_TIER, AUTH_SAMPLE_SIZE - out.size()));
        build_tier_sample(Supernode::TIER4_STAKE_AMOUNT, std::numeric_limits<uint64_t>::max(),
                          std::min(ITEMS_PER_TIER, AUTH_SAMPLE_SIZE - out.size()));
    }

    std::string auth_sample_str;
    for (const auto &a : out) {
        auth_sample_str += a->walletAddress() + "\n";
    }
    LOG_PRINT_L0("known supernodes: " << this->size());
    LOG_PRINT_L0("auth sample: \n" << auth_sample_str);


    return out.size() == AUTH_SAMPLE_SIZE;
}

vector<string> FullSupernodeList::items() const
{
    vector<string> result;
    result.reserve(m_list.size());
    boost::shared_lock<boost::shared_mutex> readerLock(m_access);
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

bool FullSupernodeList::bestSupernode(vector<SupernodePtr> &arg, const crypto::hash &block_hash, SupernodePtr &result)
{
    if (arg.size() == 0) {
        LOG_ERROR("empty input");
        return false;
    }

    vector<SupernodePtr>::iterator best = max_element(arg.begin(), arg.end(), [&](const SupernodePtr a, const SupernodePtr b) {
        crypto::hash hash_a, hash_b;
        a->getScoreHash(block_hash, hash_a);
        b->getScoreHash(block_hash, hash_b);
        uint256_t a_value = hash_to_int256(hash_a);
        uint256_t b_value = hash_to_int256(hash_b);
        return a_value < b_value;

    });
    result = *best;
    arg.erase(best);
    return true;
}



void FullSupernodeList::selectTierSupernodes(const crypto::hash &block_hash, uint64_t tier_min_stake, uint64_t tier_max_stake,
                                             vector<SupernodePtr> &output, const vector<SupernodePtr> &selected_items,
                                             size_t max_items)
{
    // copy all the items with the stake not less than given tier_min_stake
    vector<SupernodePtr> all_tier_items;

    {
        boost::shared_lock<boost::shared_mutex> readerLock(m_access);
        for (const auto &it : m_list) {
            size_t seconds_since_last_update =  size_t(std::time(nullptr)) - it.second->lastUpdateTime();
            MDEBUG("checking supernode " << it.first << ", updated: " << seconds_since_last_update << " seconds ago"
                   << ", stake amount: " << it.second->stakeAmount());
            if (seconds_since_last_update < ANNOUNCE_TTL_SECONDS
                    && it.second->stakeAmount() >= tier_min_stake
                    && it.second->stakeAmount() < tier_max_stake
                    && find_if(selected_items.begin(), selected_items.end(), [&](const auto &sn) {
                               return sn->walletAddress() == it.first;
                       }) == selected_items.end()) {
                MDEBUG("supernode: " << it.first << " selected for auth sample");
                all_tier_items.push_back(it.second);
            }

        }
    }
    if (all_tier_items.empty()) {
        LOG_PRINT_L1("No items selected for tier:  " << tier_min_stake << " - " << tier_max_stake);
        return;
    }

    for (int i = 0; i < max_items; ++i) {
        SupernodePtr best;
        if (bestSupernode(all_tier_items, block_hash, best)) {
            output.push_back(best);
        } else {
            LOG_ERROR("Can't select best supernode");
            break;
        }
    }

}

bool FullSupernodeList::loadWallet(const std::string &wallet_path)
{
    bool result = false;
    Supernode * sn = Supernode::load(wallet_path, "", m_daemon_address, m_testnet);
    if (sn)  {
        if (!this->add(sn)) {
            LOG_ERROR("Can't add supernode " << sn->walletAddress() << ", already exists");
            delete sn;
        } else {
            LOG_PRINT_L1("Added supernode: " << sn->walletAddress() << ", stake: " << sn->stakeAmount());
            result = true;
        }
    }
    return result;
}


}
