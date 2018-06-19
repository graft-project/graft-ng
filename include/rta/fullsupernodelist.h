#ifndef FULLSUPERNODELIST_H
#define FULLSUPERNODELIST_H

#include <rta/supernode.h>
#include <cryptonote_config.h>

#include <string>
#include <vector>
#include <boost/shared_ptr.hpp>
#include <unordered_map>




namespace graft {



class FullSupernodeList
{
public:
    static const uint8_t AUTH_SAMPLE_SIZE = 8;
//    50,000 GRFT –  tier 1
//    90,000 GRFT –  tier 2
//    150,000 GRFT – tier 3
//    250,000 GRFT – tier 4
    static const uint64_t TIER1_STAKE_AMOUNT = COIN * 50000;
    static const uint64_t TIER2_STAKE_AMOUNT = COIN * 90000;
    static const uint64_t TIER3_STAKE_AMOUNT = COIN * 150000;
    static const uint64_t TIER4_STAKE_AMOUNT = COIN * 250000;
    static const size_t   ITEMS_PER_TIER = 2;
    static const uint64_t AUTH_SAMPLE_HASH_HEIGHT = 20;

    using SupernodePtr = boost::shared_ptr<Supernode>;

    FullSupernodeList();
    ~FullSupernodeList();

    bool add(Supernode * item);
    bool remove(const std::string &address);
    size_t size() const;
    bool exists(const std::string &address) const;
    bool update(const std::string &address, const std::vector<Supernode::KeyImage> &key_images);
    SupernodePtr get(const std::string &address) const;
    bool buildAuthSample(std::vector<SupernodePtr> &out);
    bool setDaemonAddress(const std::string & address);

private:
    bool getBlockHash(uint64_t height, crypto::hash &hash);
    void selectTierSupernodes(const crypto::hash &block_hash, uint64_t tier_min_stake,
                              std::vector<SupernodePtr> &output);
    bool bestSupernode(std::vector<SupernodePtr> &arg, const crypto::hash &block_hash, SupernodePtr &result);

private:
    std::unordered_map<std::string, SupernodePtr> m_list;
};

} // namespace graft

#endif // FULLSUPERNODELIST_H
