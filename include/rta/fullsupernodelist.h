#ifndef FULLSUPERNODELIST_H
#define FULLSUPERNODELIST_H

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
    static const uint8_t  AUTH_SAMPLE_SIZE = 4;
    static const size_t   ITEMS_PER_TIER = 1;
    static const uint64_t AUTH_SAMPLE_HASH_HEIGHT = 20; // block number for calculating auth sample should be calculated as current block height - AUTH_SAMPLE_HASH_HEIGHT;
    static const uint64_t ANNOUNCE_TTL_SECONDS = 60 * 60; // if more than ANNOUNCE_TTL_SECONDS passed from last annouce - supernode excluded from auth sample selection

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
    bool update(const std::string &address, const std::vector<Supernode::SignedKeyImage> &key_images);

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


private:
    void selectTierSupernodes(const crypto::hash &block_hash, uint64_t tier_min_stake, uint64_t tier_max_stake,
                              std::vector<SupernodePtr> &output, const std::vector<SupernodePtr> &selected_items,
                              size_t max_items);
    bool bestSupernode(std::vector<SupernodePtr> &arg, const crypto::hash &block_hash, SupernodePtr &result);

    bool loadWallet(const std::string &wallet_path);

private:
    std::unordered_map<std::string, SupernodePtr> m_list;
    std::string m_daemon_address;
    bool m_testnet;
    DaemonRpcClient m_rpc_client;
    mutable boost::shared_mutex m_access;
    std::unique_ptr<utils::ThreadPool> m_tp;
    std::atomic_size_t m_refresh_counter;
};

using FullSupernodeListPtr = boost::shared_ptr<FullSupernodeList>;


} // namespace graft

#endif // FULLSUPERNODELIST_H
