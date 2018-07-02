#ifndef SUPERNODE_H
#define SUPERNODE_H

#include <string>
#include <vector>
#include <crypto/crypto.h>
#include <cryptonote_basic/cryptonote_basic.h>
#include <wallet/wallet2.h>


namespace graft {

/*!
 * \brief The Supernode class - Representing supernode instance
 */
class Supernode
{
public:
    using KeyImage = std::pair<crypto::key_image, crypto::signature>;

    //  50,000 GRFT –  tier 1
    //  90,000 GRFT –  tier 2
    //  150,000 GRFT – tier 3
    //  250,000 GRFT – tier 4
    static const uint64_t TIER1_STAKE_AMOUNT = COIN *  50000;
    static const uint64_t TIER2_STAKE_AMOUNT = COIN *  90000;
    static const uint64_t TIER3_STAKE_AMOUNT = COIN * 150000;
    static const uint64_t TIER4_STAKE_AMOUNT = COIN * 250000;

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
    void refresh();

    /*!
     * \brief testnet        - to check if wallet is testnet wallet
     * \return               - true if testnet
     */
    bool testnet() const;


    /*!
     * \brief stakeAmount - returns stake amount
     * \return            - stake amount in atomic units
     */
    uint64_t stakeAmount() const;
    /*!
     * \brief walletAddress - returns wallet address as string
     * \return
     */
    std::string walletAddress() const;

    /*!
     * \brief exportKeyImages - exports key images
     * \param key_images      - destination vector
     * \return                - true on success
     */
    bool exportKeyImages(std::vector<Supernode::KeyImage> &key_images) const;


    /*!
     * \brief importKeyImages - imports key images
     * \param key_images      - source vector
     * \return                - true on success
     */
    bool importKeyImages(const std::vector<KeyImage> &key_images);

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

    static Supernode * load(const std::string &wallet_path, const std::string &wallet_password, const std::string &daemon_address, bool testnet = false,
                                       const std::string &seed_language = std::string());

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

    /*!
     * \brief verifySignature - verifies signature
     * \param msg             - message to verify
     * \param signature       - signer's signature
     * \return                - true if signature valid
     */
    bool verifySignature(const std::string &msg, const string &address, const crypto::signature &signature) const;

    /*!
     * \brief getScoreHash  - calculates supernode score (TODO: as 265-bit integer)
     * \param block_hash    - block hash used in calculation
     * \param result        - result will be written here;
     * \return              - true on success
     */
    void getScoreHash(const crypto::hash &block_hash, crypto::hash &result) const;


private:
    Supernode(bool testnet = false);

private:
    tools::wallet2 m_wallet;
};

}

#endif // SUPERNODE_H
