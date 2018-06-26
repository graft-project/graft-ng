#include "fullsupernodelist.h"

#include <algorithm>
#include <boost/multiprecision/cpp_int.hpp>
#include <boost/filesystem.hpp>
#include <misc_log_ex.h>
// using WalletManager for
#include <wallet/api/wallet_manager.h>
#include <iostream>
#include <thread_pool/thread_pool.hpp>
#include <future>

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

FullSupernodeList::FullSupernodeList(const string &daemon_address, bool testnet)
    : m_testnet(testnet)
    , m_daemon_address(daemon_address)
    , m_rpc_client(daemon_address, "", "")
{

}

FullSupernodeList::~FullSupernodeList()
{
    boost::unique_lock<boost::shared_mutex> writerLock(m_access);
    m_list.clear();
}

bool FullSupernodeList::add(Supernode *item)
{

    if (exists(item->walletAddress()))
        return false;

    boost::unique_lock<boost::shared_mutex> writerLock(m_access);
    m_list.insert(std::make_pair(item->walletAddress(), SupernodePtr{item}));
    LOG_PRINT_L0("added supernode: " << item->walletAddress());
    LOG_PRINT_L0("list size: " << m_list.size());
    return true;
}

size_t FullSupernodeList::loadFromDir(const string &base_dir)
{
    vector<string> wallets = findWallets(base_dir);
    size_t result = 0;
    LOG_PRINT_L0("found wallets: " << wallets.size());
    for (const auto &wallet_path : wallets) {
        Supernode * sn = Supernode::load(wallet_path, "", m_daemon_address, m_testnet);
        if (sn)  {
            if (!this->add(sn)) {
                LOG_ERROR("Can't add supernode " << sn->walletAddress() << ", already exists");
                delete sn;
            } else {
                LOG_PRINT_L1("Added supernode: " << sn->walletAddress() << ", stake: " << sn->stakeAmount());
                ++result;
            }
        }
    }
    return result;
}

size_t FullSupernodeList::loadFromDirThreaded(const string &base_dir, size_t &found_wallets)
{
    vector<string> wallets = findWallets(base_dir);
    LOG_PRINT_L0("found wallets: " << wallets.size());
    found_wallets = wallets.size();

    auto worker = [&](const std::string &wallet_path) {
        Supernode * sn = Supernode::load(wallet_path, "", m_daemon_address, m_testnet);
        if (sn) {
            if (!this->add(sn)) {
                LOG_ERROR("can't add supernode: " << sn->walletAddress());
                delete sn;
            } else {
                LOG_PRINT_L0("Loaded supernode: " << sn->walletAddress());
            }
        } else {
            LOG_ERROR("Error loading supernode wallet from: " << wallet_path);
        }
    };
    {
        tp::ThreadPool thread_pool;
        for (const auto &wallet_path : wallets) {
            LOG_PRINT_L0("posting wallet to the thread pool" << wallet_path);
            thread_pool.post(boost::bind<void>(worker, wallet_path), true);
            LOG_PRINT_L0("posted wallet " << wallet_path);
        }
        LOG_PRINT_L0("posted " << wallets.size() << " jobs");
    }
    return this->size();
}


//size_t FullSupernodeList::loadFromDirThreaded(const string &base_dir, size_t &found_wallets)
//{
//    vector<string> wallets = findWallets(base_dir);
//    LOG_PRINT_L0("found wallets: " << wallets.size());
//    found_wallets = wallets.size();
//    boost::asio::io_service ioservice;
//    boost::thread_group threadpool;
//    size_t threads = boost::thread::hardware_concurrency();
//    std::unique_ptr<boost::asio::io_service::work> work(new boost::asio::io_service::work(ioservice));

//    for (int i = 0; i < threads; i++) {
//        threadpool.create_thread(boost::bind(&boost::asio::io_service::run, &ioservice));
//    }

//    auto worker = [&](const std::string &wallet_path) {
//        Supernode * sn = Supernode::load(wallet_path, "", m_daemon_address, m_testnet);
//        if (sn) {
//            if (!this->add(sn)) {
//                LOG_ERROR("can't add supernode: " << sn->walletAddress());
//                delete sn;
//            } else {
//                LOG_PRINT_L0("Loaded supernode: " << sn->walletAddress());
//            }
//        } else {
//            LOG_ERROR("Error loading supernode wallet from: " << wallet_path);
//        }
//    };

//    for (const auto &wallet_path : wallets) {
//        LOG_PRINT_L1("posting wallet " << wallet_path);
//        ioservice.dispatch(boost::bind<void>(worker, wallet_path));
//        LOG_PRINT_L1("wallet sent to thread pool " << wallet_path);
//    }

//    work.reset();
//    while (!ioservice.stopped())
//        ioservice.poll();
//    threadpool.join_all();
//    ioservice.stop();

//    return this->size();
//}

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

bool FullSupernodeList::update(const string &address, const std::vector<Supernode::KeyImage> &key_images)
{

    boost::unique_lock<boost::shared_mutex> writerLock(m_access);
    auto it = m_list.find(address);
    if (it != m_list.end()) {
        return it->second->importKeyImages(key_images);
    }
    return false;
}

FullSupernodeList::SupernodePtr FullSupernodeList::get(const string &address) const
{
    boost::shared_lock<boost::shared_mutex> readerLock(m_access);
    auto it = m_list.find(address);
    if (it != m_list.end())
        return it->second;
    return SupernodePtr(nullptr);
}

bool FullSupernodeList::buildAuthSample(uint64_t height, std::vector<FullSupernodeList::SupernodePtr> &out)
{
    crypto::hash block_hash;
    std::string  block_hash_str;

    if (!getBlockHash(height, block_hash_str)) {
        LOG_ERROR("getBlockHash error");
        return false;
    }

    epee::string_tools::hex_to_pod(block_hash_str, block_hash);
    std::vector<SupernodePtr> tier_supernodes;


    auto out_it = std::back_inserter(out);

    auto build_tier_sample = [&](uint64_t tier_min, uint64_t tier_max) {
        selectTierSupernodes(block_hash, tier_min, tier_max, tier_supernodes);
        std::copy(tier_supernodes.begin(), tier_supernodes.end(), out_it);
        tier_supernodes.clear();
    };

    build_tier_sample(Supernode::TIER1_STAKE_AMOUNT, Supernode::TIER2_STAKE_AMOUNT);
    build_tier_sample(Supernode::TIER2_STAKE_AMOUNT, Supernode::TIER3_STAKE_AMOUNT);
    build_tier_sample(Supernode::TIER3_STAKE_AMOUNT, Supernode::TIER4_STAKE_AMOUNT);
    build_tier_sample(Supernode::TIER4_STAKE_AMOUNT, std::numeric_limits<uint64_t>::max());

    return true;
}

std::vector<string> FullSupernodeList::items() const
{
    vector<string> result;
    result.reserve(m_list.size());
    boost::shared_lock<boost::shared_mutex> readerLock(m_access);
    for (auto const& it: m_list)
        result.push_back(it.first);

    return result;
}

bool FullSupernodeList::getBlockHash(uint64_t height, std::string &hash)
{
    bool result = m_rpc_client.get_block_hash(height, hash);
    return result;
}

void FullSupernodeList::refreshAsync()
{
    std::async(std::launch::async, [&]() {
        tp::ThreadPool thread_pool;
        auto worker = [&](const std::string &address) {
            SupernodePtr sn = this->get(address);
            if (sn)
                sn->refresh();
        };
        for (const auto & address : this->items()) {
            worker(address);
        }
    });
}

bool FullSupernodeList::bestSupernode(std::vector<SupernodePtr> &arg, const crypto::hash &block_hash, SupernodePtr &result)
{
    if (arg.size() == 0) {
        LOG_ERROR("empty input");
        return false;
    }

    std::vector<SupernodePtr>::iterator best = std::max_element(arg.begin(), arg.end(), [&](const SupernodePtr a, const SupernodePtr b) {
        crypto::hash hash_a, hash_b;
        a->getScoreHash(block_hash, hash_a);
        b->getScoreHash(block_hash, hash_b);
        uint256_t a_value = hash_to_int256(hash_a);
        uint256_t b_value = hash_to_int256(hash_b);
        // LOG_PRINT_L0("a_value: " << a_value << ", b_value: " << b_value);
        return a_value < b_value;

    });
    result = *best;
    arg.erase(best);
    return true;
}

void FullSupernodeList::selectTierSupernodes(const crypto::hash &block_hash, uint64_t tier_min_stake, uint64_t tier_max_stake,
                                             std::vector<SupernodePtr> &output)
{
    // copy all the items with the stake not less than given tier_min_stake
    std::vector<SupernodePtr> all_tier_items;

    {
        boost::shared_lock<boost::shared_mutex> readerLock(m_access);
        for (const auto &it : m_list) {
            if (it.second->stakeAmount() >= tier_min_stake && it.second->stakeAmount() < tier_max_stake)
                all_tier_items.push_back(it.second);
        }
    }

    for (int i = 0; i < ITEMS_PER_TIER; ++i) {
        SupernodePtr best;
        if (bestSupernode(all_tier_items, block_hash, best)) {
            output.push_back(best);
        } else {
            LOG_ERROR("Can't select best supernode");
            break;
        }
    }
}

}
