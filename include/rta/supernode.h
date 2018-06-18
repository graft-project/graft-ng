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
    /*!
     * \brief Supernode - constructs supernode
     * \param wallet_path - filename of the existing wallet or new wallet. in case filename doesn't exists, new wallet will be created
     * \param wallet_password  - wallet's password. wallet_path doesn't exists - this password will be used to protect new wallet;
     * \param daemon_address  - address of the cryptonode daemon
     */
    Supernode(const std::string &wallet_path, const std::string &wallet_password, const std::string &daemon_address,
              const std::string &seed_language = std::string());
    ~Supernode();
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
    bool exportKeyImages(std::vector<KeyImage> &key_images);

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
     * \return                         - pointer to Supernode object on success
     */
    static Supernode * createFromViewOnlyWallet(const std::string &path,
                                       const cryptonote::account_public_address &address,
                                       const crypto::secret_key& viewkey = crypto::secret_key());

    crypto::secret_key exportViewkey();

    // bool signMessage(const std::string &msg, crypto::signature &signature);
    // bool verifySignature();
    bool connectToDaemon(const std::string &address);

private:
    Supernode();

private:
    tools::wallet2 m_wallet;
};

}

#endif // SUPERNODE_H
