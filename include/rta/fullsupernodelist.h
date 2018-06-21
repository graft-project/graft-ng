#ifndef FULLSUPERNODELIST_H
#define FULLSUPERNODELIST_H

#include <rta/supernode.h>
#include <rta/DaemonRpcClient.h>
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
//  50,000 GRFT –  tier 1
//  90,000 GRFT –  tier 2
//  150,000 GRFT – tier 3
//  250,000 GRFT – tier 4
    static const uint64_t TIER1_STAKE_AMOUNT = COIN *  50000;
    static const uint64_t TIER2_STAKE_AMOUNT = COIN *  90000;
    static const uint64_t TIER3_STAKE_AMOUNT = COIN * 150000;
    static const uint64_t TIER4_STAKE_AMOUNT = COIN * 250000;
    static const size_t   ITEMS_PER_TIER = 2;
    static const uint64_t AUTH_SAMPLE_HASH_HEIGHT = 20; // block number for calculating auth sample should be calculated as current block height - AUTH_SAMPLE_HASH_HEIGHT;

    using SupernodePtr = boost::shared_ptr<Supernode>;

    FullSupernodeList(const std::string &daemon_address, bool testnet = false);
    ~FullSupernodeList();
    /**
     * @brief add - adds supernode object to a list and owns it. caller doesn't need to delete an object
     * @param item - pointer to a Supernode object
     * @return true if item was added. false if already in list
     */
    bool add(Supernode * item);

    /*!
     * \brief loadFromDir - loads list from a directory. Directory should contain wallet key files
     * \param base_dir    - path to the base directory with wallet files
     * \return            - number of loaded supernode wallets
     */
    size_t loadFromDir(const std::string &base_dir);
    /*!
     * \brief remove  - removes Supernode from list. closes it's wallet and frees memory
     * \param address - supernode address
     * \return        - true if supernode removed
     */
    bool remove(const std::string &address);

    /*!
     * \brief size  - number of supernodes in list
     * \return
     */
    size_t size() const;

    /*!
     * \brief exists  - checks if supernode with given address exists in list
     * \param address - supernode address
     * \return        - true if exists
     */
    bool exists(const std::string &address) const;

    /*!
     * \brief update     - updates supernode's key images. this will probably cause stake amount change
     * \param address    - supernode's address
     * \param key_images - list of key images
     * \return           - true of successfully updated
     */
    bool update(const std::string &address, const std::vector<Supernode::KeyImage> &key_images);

    /*!
     * \brief get      - returns supernode instance (pointer)
     * \param address  - supernode's address
     * \return         - shared pointer to supernode or empty pointer (nullptr) is no such address
     */
    SupernodePtr get(const std::string &address) const;

    /*!
     * \brief buildAuthSample - builds auth sample (8 supernodes) for given block height
     * \param height          - block height used to perform selection
     * \param out             - vector of supernode pointers
     * \return                - true on success
     */
    bool buildAuthSample(uint64_t height, std::vector<SupernodePtr> &out);

    /*!
     * \brief items - returns address list of known supernodes
     * \return
     */
    std::vector<std::string> items() const;

    /*!
     * \brief getBlockHash - returns block hash for given height
     * \param height       - block height
     * \param hash         - output hash value
     * \return             - true on success
     */
    bool getBlockHash(uint64_t height, std::string &hash);

private:
    void selectTierSupernodes(const crypto::hash &block_hash, uint64_t tier_min_stake, uint64_t tier_max_stake,
                              std::vector<SupernodePtr> &output);
    bool bestSupernode(std::vector<SupernodePtr> &arg, const crypto::hash &block_hash, SupernodePtr &result);

private:
    std::unordered_map<std::string, SupernodePtr> m_list;
    std::string m_daemon_address;
    bool m_testnet;
    DaemonRpcClient m_rpc_client;
};

} // namespace graft

#endif // FULLSUPERNODELIST_H
