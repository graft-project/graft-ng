#include "fullsupernodelist.h"

#include <algorithm>
#include <boost/multiprecision/cpp_int.hpp>
#include <misc_log_ex.h>

using namespace boost::multiprecision;

namespace {
    uint256_t hash_to_int256(const crypto::hash &hash)
    {
        cryptonote::blobdata str_val = std::string("0x") + epee::string_tools::pod_to_hex(hash);
        return uint256_t(str_val);
    }
}

namespace graft {

FullSupernodeList::FullSupernodeList()
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

bool FullSupernodeList::buildAuthSample(std::vector<FullSupernodeList::SupernodePtr> &out)
{
    crypto::hash block_hash;
    getBlockHash(AUTH_SAMPLE_HASH_HEIGHT, block_hash);

    std::vector<SupernodePtr> tier_supernodes;

    std::vector<uint64_t> tier_stack_amounts {TIER4_STAKE_AMOUNT, TIER3_STAKE_AMOUNT, TIER2_STAKE_AMOUNT, TIER1_STAKE_AMOUNT};
    auto out_it = out.begin();
    for (auto amount : tier_stack_amounts) {
        selectTierSupernodes(block_hash, amount, tier_supernodes);
        out_it = std::copy(tier_supernodes.begin(), tier_supernodes.end(), out_it);
        tier_supernodes.clear();
    }
}

bool FullSupernodeList::getBlockHash(uint64_t height, crypto::hash &hash)
{
    // TODO: call corresponding RPC;
    return true;
}

bool FullSupernodeList::bestSupernode(std::vector<SupernodePtr> &arg, const crypto::hash &block_hash, SupernodePtr &result)
{
    if (arg.size() == 0)
        return false;

    std::vector<SupernodePtr>::iterator best = std::max_element(arg.begin(), arg.end(), [&](const SupernodePtr a, const SupernodePtr b) {
        crypto::hash hash_a, hash_b;
        a->getScoreHash(block_hash, hash_a);
        b->getScoreHash(block_hash, hash_b);
        return hash_to_int256(hash_a) < hash_to_int256(hash_b);
    });
    result = *best;
    arg.erase(best);
}

void FullSupernodeList::selectTierSupernodes(const crypto::hash &block_hash, uint64_t tier_min_stake,
                                             std::vector<SupernodePtr> &output)
{
    // copy all the items with the stake not less than given tier_min_stake
    std::vector<SupernodePtr> all_tier_items;
    for (const auto &it : m_list) {
        if (it.second->stakeAmount() >= tier_min_stake)
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
