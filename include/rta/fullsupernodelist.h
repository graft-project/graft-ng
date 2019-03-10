#ifndef FULLSUPERNODELIST_H
#define FULLSUPERNODELIST_H

#include <random>

#include "rta/supernode.h"
#include "rta/DaemonRpcClient.h"

#include <cryptonote_config.h>
#include <string>
#include <vector>
#include <future>
#include <unordered_map>

#include <boost/shared_ptr.hpp>
#include <boost/thread.hpp>


namespace graft {

namespace utils {
    class ThreadPool;
}


class FullSupernodeList
{
public:
    static constexpr int32_t TIERS = 4;
    static constexpr int32_t ITEMS_PER_TIER = 1;
    static constexpr int32_t AUTH_SAMPLE_SIZE = TIERS * ITEMS_PER_TIER;
    static constexpr int64_t AUTH_SAMPLE_HASH_HEIGHT = 20; // block number for calculating auth sample should be calculated as current block height - AUTH_SAMPLE_HASH_HEIGHT;
    static constexpr int64_t ANNOUNCE_TTL_SECONDS = 60 * 60; // if more than ANNOUNCE_TTL_SECONDS passed from last annouce - supernode excluded from auth sample selection

    FullSupernodeList(const std::string &daemon_address, bool testnet = false);
    ~FullSupernodeList();
    /**
     * @brief add - adds supernode object to a list and owns it. caller doesn't need to delete an object
     * @param item - pointer to a Supernode object
     * @return true if item was added. false if already in list
     */
    bool add(Supernode * item);

    bool add(SupernodePtr item);
    /*!
     * \brief loadFromDir - loads list from a directory. Directory should contain wallet key files
     * \param base_dir    - path to the base directory with wallet files
     * \return            - number of loaded supernode wallets
     */
    size_t loadFromDir(const std::string &base_dir);
    
    /*!
     * \brief loadFromDirThreaded - loads list from directory.
     * \param base_dir            - directory where to search for wallets
     * \param found_wallets       - number of found wallets
     * \return                    - number of loaded supernodes
     */
    size_t loadFromDirThreaded(const std::string &base_dir, size_t &found_wallets);
    
    /*!
     * \brief remove  - removes Supernode from list. closes it's wallet and frees memory
     * \param id      - supernode id
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
     * \param id      - supernode id
     * \return        - true if exists
     */
    bool exists(const std::string &id) const;

    /*!
     * \brief get      - returns supernode instance (pointer)
     * \param id       - supernode's public id
     * \return         - shared pointer to supernode or empty pointer (nullptr) is no such address
     */
    SupernodePtr get(const std::string &id) const;

    typedef std::vector<SupernodePtr> supernode_array;

    /*!
     * \brief buildAuthSample - builds auth sample (8 supernodes) for given block height
     * \param height          - block height used to perform selection
     * \param payment_id      - payment id which is used for building auth sample
     * \param out             - vector of supernode pointers
     * \return                - true on success
     */
    bool buildAuthSample(uint64_t height, const std::string& payment_id, supernode_array &out);

    bool buildAuthSample(const std::string& payment_id, supernode_array &out);

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

    /*!
     * \brief refreshAsync - starts asynchronous parallel refresh all supernodes using internal threadpool.
     *                       number of parallel jobs equals to number of hardware CPU cores
     *
     * \return             - std::future to wait for result
     */
    std::future<void> refreshAsync();

    /*!
     * \brief refreshedItems - returns number of refreshed supernodes
     * \return
     */
    size_t refreshedItems() const;

    typedef std::vector<supernode_stake> supernode_stake_array;

    /*!
     * \brief updateStakes - update stakes
     * \param              - array of stakes
     * \return
     */
    void updateStakes(const supernode_stake_array& stakes, const std::string& cryptonode_rpc_address, bool testnet);

    struct blockchain_based_list_entry
    {
        std::string supernode_public_id;
        std::string supernode_public_address;
        uint64_t    amount;
    };
    
    typedef std::vector<blockchain_based_list_entry> blockchain_based_list_tier;
    typedef std::vector<blockchain_based_list_tier>  blockchain_based_list;
    typedef std::shared_ptr<blockchain_based_list>   blockchain_based_list_ptr;

    /*!
     * \brief setBlockchainBasedList - updates full list of supernodes
     * \return
     */
    void setBlockchainBasedList(uint64_t block_number, const blockchain_based_list_ptr& list);

    /*!
     * \brief blockchainBasedListMaxBlockNumber - number of latest block which blockchain list is built for
     * \return
     */
    uint64_t getBlockchainBasedListMaxBlockNumber() const;

    /*!
     * \brief synchronizeWithCryptonode - synchronize with cryptonode
     * \return
     */
    void synchronizeWithCryptonode(const char* supernode_network_address, const char* supernode_address);

    /*!
     * \brief getBlockchainHeight - returns current daemon block height
     * \return
     */
    uint64_t getBlockchainHeight() const;

private:
    // bool loadWallet(const std::string &wallet_path);
    void addImpl(SupernodePtr item);
    bool selectSupernodes(size_t items_count, const std::string& payment_id, const blockchain_based_list_tier& src_array, supernode_array& dst_array);    

    typedef std::unordered_map<uint64_t, blockchain_based_list_ptr> blockchain_based_list_map;

    blockchain_based_list_ptr findBlockchainBasedList(uint64_t block_number) const;

private:
    // key is public id as a string
    std::unordered_map<std::string, SupernodePtr> m_list;
    std::string m_daemon_address;
    bool m_testnet;
    mutable DaemonRpcClient m_rpc_client;
    mutable boost::shared_mutex m_access;
    std::unique_ptr<utils::ThreadPool> m_tp;
    std::atomic_size_t m_refresh_counter;
    uint64_t m_blockchain_based_list_max_block_number;
    blockchain_based_list_map m_blockchain_based_lists;
    std::mt19937_64 m_rng;
    boost::posix_time::ptime m_last_recv_stakes;
    boost::posix_time::ptime m_last_recv_blockchain_based_list;
};

using FullSupernodeListPtr = boost::shared_ptr<FullSupernodeList>;

std::ostream& operator<<(std::ostream& os, const std::vector<SupernodePtr> supernodes);


} // namespace graft

#endif // FULLSUPERNODELIST_H
