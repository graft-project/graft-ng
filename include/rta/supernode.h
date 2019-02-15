#ifndef SUPERNODE_H
#define SUPERNODE_H

#include <crypto/crypto.h>
#include <cryptonote_config.h>
#include <boost/scoped_ptr.hpp>
#include <boost/thread/shared_mutex.hpp>
#include <boost/asio/io_service.hpp>
#include <string>
#include <vector>

namespace tools {
    class wallet2;
}

namespace cryptonote {
    class transaction;
}

namespace graft::supernode::request { struct SupernodeAnnounce; }

namespace graft {
/*!
 * \brief The Supernode class - Representing supernode instance
 */
class Supernode
{
public:
    using SignedKeyImage = std::pair<crypto::key_image, crypto::signature>;

    //  50,000 GRFT –  tier 1
    //  90,000 GRFT –  tier 2
    //  150,000 GRFT – tier 3
    //  250,000 GRFT – tier 4
    static constexpr uint64_t TIER1_STAKE_AMOUNT = COIN *  50000;
    static constexpr uint64_t TIER2_STAKE_AMOUNT = COIN *  90000;
    static constexpr uint64_t TIER3_STAKE_AMOUNT = COIN * 150000;
    static constexpr uint64_t TIER4_STAKE_AMOUNT = COIN * 250000;

    /*!
     * \brief Supernode - constructs supernode
     * \param wallet_path - filename of the existing wallet or new wallet. in case filename doesn't exists, new wallet will be created
     * \param wallet_password  - wallet's password. wallet_path doesn't exists - this password will be used to protect new wallet;
     * \param daemon_address  - address of the cryptonode daemon
     * \param testnet         - testnet flag
     * \param seed_language   - seed language
     */
    Supernode(const std::string &wallet_path, const std::string &wallet_password, const std::string &daemon_address, bool testnet = false,
              const std::string &seed_language = std::string());
    ~Supernode();

    /*!
     * \brief setDaemonAddress - setup connection with the cryptonode daemon
     * \param address          - address in "hostname:port" form
     * \return                 - true on success
     */
    bool setDaemonAddress(const std::string &address);

    /*!
     * \brief refresh         - get latest blocks from the daemon
     * \return                - true on success
     */
    bool refresh();

    /*!
     * \brief testnet        - to check if wallet is testnet wallet
     * \return               - true if testnet
     */
    bool testnet() const;


    /*!
     * \brief stakeAmount - returns stake amount, i.e. the wallet balance that only counts verified-unspent inputs.
     * \return            - stake amount in atomic units
     */
    uint64_t stakeAmount() const;
    /*!
     * \brief tier - returns the tier of this supernode based on its stake amount
     * \return     - the tier (1-4) of the supernode or 0 if the verified stake amount is below tier 1
     */
    uint32_t tier() const;
    /*!
     * \brief walletBalance - returns wallet balance as seen by the internal wallet; note that this
     *                        can be wrong for a view-only wallet with unverified transactions: you
     *                        typically want to use stakeAmount() instead.
     * \return              - wallet balance in atomic units
     */
    uint64_t walletBalance() const;
    /*!
     * \brief walletAddress - returns wallet address as string
     * \return
     */
    std::string walletAddress() const;

    /*!
     * \brief daemonHeight - returns cryptonode's blockchain height
     * \return
     */
    uint64_t daemonHeight() const;

    /*!
     * \brief exportKeyImages - exports key images
     * \param key_images      - destination vector
     * \return                - true on success
     */
    bool exportKeyImages(std::vector<Supernode::SignedKeyImage> &key_images) const;


    /*!
     * \brief importKeyImages - imports key images
     * \param key_images      - source vector
     * \param height          - output height
     * \return                - true on success
     */
    bool importKeyImages(const std::vector<SignedKeyImage> &key_images, uint64_t &height);

    /*!
     * \brief createFromViewOnlyWallet - creates new Supernode object, creates underlying read-only stake wallet
     * \param path                     - path to wallet files to be created
     * \param account_public_address   - public address of read-only wallet
     * \param viewkey                  - private view key
     * \param testnet                  - testnet flag
     * \return                         - pointer to Supernode object on success
     */
    static Supernode * createFromViewOnlyWallet(const std::string &path,
                                       const std::string &address,
                                       const crypto::secret_key& viewkey = crypto::secret_key(), bool testnet = false);

    /*!
     * \brief load              - creates new Supernode object from existing wallet
     * \param wallet_path       - path to existing  wallet file
     * \param wallet_password   - wallet password
     * \param daemon_address    - daemon address connection for supernode
     * \param testnet           - testnet flag
     * \param seed_language     - seed language
     * \return                  - Supernode pointer on success
     */
    static Supernode * load(const std::string &wallet_path, const std::string &wallet_password, const std::string &daemon_address, bool testnet = false,
                                       const std::string &seed_language = std::string());

    /*!
     * \brief updateFromAnnounce - updates supernode from announce (helper to extract signed key images from graft::supernode::request::SupernodeAnnounce)
     * \param announce           - reference to graft::supernode::request::SupernodeAnnounce
     * \return                   - true on success
     */
    bool updateFromAnnounce(const graft::supernode::request::SupernodeAnnounce& announce);

    /*!
     * \brief createFromAnnounce - creates new Supernode instance from announce
     * \param path               - wallet file path
     * \param announce           - announce object
     * \param testnet            - testnet flag
     * \return                   - Supernode pointer on success
     */
    static Supernode * createFromAnnounce(const std::string &path,
                                          const graft::supernode::request::SupernodeAnnounce& announce,
                                          const std::string &daemon_address,
                                          bool testnet);

    bool prepareAnnounce(graft::supernode::request::SupernodeAnnounce& announce);

    /*!
     * \brief exportViewkey - exports stake wallet private viewkey
     * \return private viewkey
     */
    crypto::secret_key exportViewkey() const;

    /*!
     * \brief signMessage - signs message. internally hashes the message and signs the hash
     * \param msg         - input message
     * \param signature   - output signature
     * \return            - true if successfully signed. false if wallet is watch-only
     */
    bool signMessage(const std::string &msg, crypto::signature &signature) const;


    bool signHash(const crypto::hash &hash, crypto::signature &signature) const;
    /*!
     * \brief verifySignature - verifies signature
     * \param msg             - message to verify
     * \param signature       - signer's signature
     * \return                - true if signature valid
     */
    bool verifySignature(const std::string &msg, const std::string &address, const crypto::signature &signature) const;

    /*!
     * \brief getScoreHash  - calculates supernode score (TODO: as 265-bit integer)
     * \param block_hash    - block hash used in calculation
     * \param result        - result will be written here;
     * \return              - true on success
     */

    bool verifyHash(const crypto::hash &hash, const std::string &address, const crypto::signature &signature) const;


    void getScoreHash(const crypto::hash &block_hash, crypto::hash &result) const;

    std::string networkAddress() const;

    void setNetworkAddress(const std::string &networkAddress);
    /*!
     * \brief getAmountFromTx - scans given tx for outputs destined to this address
     * \param tx              - transaction object
     * \param amount          - amount in atomic units
     * \return                - true on success (only indicates error, amount still can be zero)
     */
    bool getAmountFromTx(const cryptonote::transaction &tx, uint64_t &amount);

    // TODO: implement me. see cryptonode/src/supernode/grafttxextra.h how payment id can be put/get from transaction object
    /*!
     * \brief getPaymentIdFromTx - returns graft payment id from given transaction
     * \param tx                 - transaction object
     * \param paymentId          - output
     * \return                   - true on success. false if some error or payment id not found in tx
     */
    bool getPaymentIdFromTx(const cryptonote::transaction &tx, std::string &paymentId);

    /*!
     * \brief validateAddress - validates wallet address
     * \param address         - address to validate
     * \param testnet         - testnet flag
     * \return                - true if address valid
     */

    static bool validateAddress(const std::string &address, bool testnet);

    /*!
     * \brief lastUpdateTime - returns timestamp when supernode updated last time
     * \return
     */
    int64_t lastUpdateTime() const;

    /*!
     * \brief setLastUpdateTime - updates wallet refresh time
     * \param time
     */
    void setLastUpdateTime(int64_t time);

    /*!
     * \brief busy - checks if stake wallet currently busy
     * \return
     */
    bool busy() const;


    static boost::shared_ptr<boost::asio::io_service> getIoService() { return  m_ioservice; }

private:
    Supernode(bool testnet = false);

private:
    using wallet2_ptr = boost::scoped_ptr<tools::wallet2>;
    mutable wallet2_ptr m_wallet;
    static boost::shared_ptr<boost::asio::io_service> m_ioservice;
    std::string    m_network_address;

    std::atomic<int64_t>       m_last_update_time;
    mutable boost::shared_mutex m_wallet_guard;

};

using SupernodePtr = boost::shared_ptr<Supernode>;

} // namespace

#endif // SUPERNODE_H
