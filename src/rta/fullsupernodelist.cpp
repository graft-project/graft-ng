#include "fullsupernodelist.h"

#include <algorithm>
#include <boost/multiprecision/cpp_int.hpp>
#include <boost/filesystem.hpp>
#include <misc_log_ex.h>
// using WalletManager for
#include <wallet/api/wallet_manager.h>

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
    m_list.clear();
}

bool FullSupernodeList::add(Supernode *item)
{
    // TODO: multithreading/locking
    if (exists(item->walletAddress()))
        return false;
    m_list.insert(std::make_pair(item->walletAddress(), SupernodePtr{item}));

    return true;
}

size_t FullSupernodeList::loadFromDir(const string &base_dir)
{
    vector<string> wallets = findWallets(base_dir);
    size_t result = 0;
    LOG_PRINT_L0("found walletsL " << wallets.size());
    for (const auto &wallet_path : wallets) {
        LOG_PRINT_L0("loading wallet: " << wallet_path);
        Supernode * sn = new Supernode(wallet_path, "", m_daemon_address, m_testnet);
        sn->refresh();
        if (sn->stakeAmount() < TIER1_STAKE_AMOUNT) {
           LOG_ERROR("wallet " << sn->walletAddress() << " doesn't have enough stake to be supernode: " << sn->stakeAmount());
           delete sn;
        } else {
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

bool FullSupernodeList::remove(const string &address)
{
    m_list.erase(address) > 0;
}

size_t FullSupernodeList::size() const
{
    return m_list.size();
}

bool FullSupernodeList::exists(const string &address) const
{
    return m_list.find(address) != m_list.end();
}

bool FullSupernodeList::update(const string &address, const std::vector<Supernode::KeyImage> &key_images)
{
    auto it = m_list.find(address);
    if (it != m_list.end()) {
        return it->second->importKeyImages(key_images);
    }
    return false;
}

FullSupernodeList::SupernodePtr FullSupernodeList::get(const string &address) const
{
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

    build_tier_sample(TIER1_STAKE_AMOUNT, TIER2_STAKE_AMOUNT);
    build_tier_sample(TIER2_STAKE_AMOUNT, TIER3_STAKE_AMOUNT);
    build_tier_sample(TIER3_STAKE_AMOUNT, TIER4_STAKE_AMOUNT);
    build_tier_sample(TIER4_STAKE_AMOUNT, std::numeric_limits<uint64_t>::max());

    return true;
}

bool FullSupernodeList::setDaemonAddress(const string &address)
{

}

std::vector<string> FullSupernodeList::items() const
{
    vector<string> result;
    result.reserve(m_list.size());

    for (auto const& it: m_list)
        result.push_back(it.first);

    return result;
}

bool FullSupernodeList::getBlockHash(uint64_t height, std::string &hash)
{
    bool result = m_rpc_client.get_block_hash(height, hash);
    return result;
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
    for (const auto &it : m_list) {
        if (it.second->stakeAmount() >= tier_min_stake && it.second->stakeAmount() < tier_max_stake)
            all_tier_items.push_back(it.second);
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
