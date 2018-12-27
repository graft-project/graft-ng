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


#ifndef __cpp_inline_variables
constexpr int32_t FullSupernodeList::TIERS, FullSupernodeList::ITEMS_PER_TIER, FullSupernodeList::AUTH_SAMPLE_SIZE;
constexpr int64_t FullSupernodeList::AUTH_SAMPLE_HASH_HEIGHT, FullSupernodeList::ANNOUNCE_TTL_SECONDS;
#endif

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
    return m_list.erase(address) > 0;
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

    std::array<std::vector<SupernodePtr>, TIERS> tier_supernodes;
    {
        boost::shared_lock<boost::shared_mutex> readerLock(m_access);
        int64_t now = static_cast<int64_t>(std::time(nullptr));
        int64_t cutoff_time = now - ANNOUNCE_TTL_SECONDS;
        for (const auto &sn_pair : m_list) {
            const auto &sn = sn_pair.second;
            const auto tier = sn->tier();
            MTRACE("checking supernode " << sn_pair.first << ", updated: " << (now - sn->lastUpdateTime()) << "s ago"
                   << ", tier: " << tier);
            if (tier > 0 && sn->lastUpdateTime() >= cutoff_time)
                tier_supernodes[tier - 1].push_back(sn);
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
        std::partial_sort(
            tier_supernodes[i].begin(), tier_supernodes[i].begin() + select[i], tier_supernodes[i].end(),
            [&](const SupernodePtr a, const SupernodePtr b) {
                crypto::hash hash_a, hash_b;
                a->getScoreHash(block_hash, hash_a);
                b->getScoreHash(block_hash, hash_b);
                return hash_to_int256(hash_a) < hash_to_int256(hash_b);
            });
        std::copy(tier_supernodes[i].begin(), tier_supernodes[i].begin() + select[i], out_it);
    }

    if (VLOG_IS_ON(2)) {
        std::string auth_sample_str, tier_sample_str;
        for (const auto &a : out) {
            auth_sample_str += a->walletAddress() + "\n";
        }
        for (size_t i = 0; i < select.size(); i++) {
            if (i > 0) tier_sample_str += ", ";
            tier_sample_str += std::to_string(select[i]) + " T"  + std::to_string(i+1);
        }
        MDEBUG("selected " << tier_sample_str << " supernodes of " << size() << " for auth sample");
        MTRACE("auth sample: \n" << auth_sample_str);
    }

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


std::ostream& operator<<(std::ostream& os, const std::vector<SupernodePtr> supernodes)
{
    for (size_t i = 0; i  < supernodes.size(); ++i) {
        os << supernodes[i]->walletAddress();
        if (i < supernodes.size() - 1)
            os << ", ";
    }
    return os;
}

}
