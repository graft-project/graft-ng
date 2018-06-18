#include "supernode.h"
#include <misc_log_ex.h>
#include <boost/filesystem.hpp>
#include <iostream>

namespace graft {

Supernode::Supernode(const std::string &wallet_path, const std::string &wallet_password, const std::string &daemon_address,
                     const std::string &seed_language)
{
    bool keys_file_exists;
    bool wallet_file_exists;
    LOG_PRINT_L3("keys_file_exists: " << std::boolalpha << keys_file_exists << std::noboolalpha
                 << "  wallet_file_exists: " << std::boolalpha << wallet_file_exists << std::noboolalpha);

    // existing wallet, open it
    if (keys_file_exists) {
        m_wallet.load(wallet_path, wallet_password);
    // new wallet, generating it
    } else {
        if (!seed_language.empty())
            m_wallet.set_seed_language(seed_language);
        crypto::secret_key recovery_val, secret_key;
        recovery_val = m_wallet.generate(wallet_path, wallet_password, secret_key, false, false);
    }
    // TODO:
    m_wallet.init(daemon_address);
}

Supernode::~Supernode()
{
    m_wallet.store();
}

uint64_t Supernode::stakeAmount() const
{
    return m_wallet.balance();
}

string Supernode::walletAddress() const
{
    return m_wallet.get_account().get_public_address_str(m_wallet.testnet());
}

bool Supernode::exportKeyImages(std::vector<Supernode::KeyImage> &key_images)
{
    key_images = m_wallet.export_key_images();
    return !key_images.empty();
}

bool Supernode::importKeyImages(const std::vector<Supernode::KeyImage> &key_images)
{
    uint64_t spent = 0, unspent = 0;
    uint64_t height = m_wallet.import_key_images(key_images, spent, unspent);
    return height > 0;
}

Supernode *Supernode::createFromViewOnlyWallet(const std::string &path, const cryptonote::account_public_address &address, const secret_key &viewkey)
{
    Supernode * result = nullptr;
    bool keys_file_exists;
    bool wallet_file_exists;
    std::string password = "";
    tools::wallet2::wallet_exists(path, keys_file_exists, wallet_file_exists);

    if (keys_file_exists) {
        LOG_ERROR("attempting to generate view only wallet, but specified file(s) exist. Exiting to not risk overwriting.");
        return result;
    }
    result = new Supernode();
    result->m_wallet.generate(path, password, address, viewkey);

    return result;
}

crypto::secret_key Supernode::exportViewkey()
{
    return m_wallet.get_account().get_keys().m_view_secret_key;
}

bool Supernode::connectToDaemon(const std::string &address)
{
    m_wallet.init(address);
}

Supernode::Supernode()
{

}

}
