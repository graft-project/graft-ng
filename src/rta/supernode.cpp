#include "supernode.h"
#include "fullsupernodelist.h"

#include <misc_log_ex.h>
#include <cryptonote_basic/cryptonote_basic_impl.h>
#include <boost/filesystem.hpp>
#include <iostream>

namespace graft {

Supernode::Supernode(const std::string &wallet_path, const std::string &wallet_password, const std::string &daemon_address, bool testnet,
                     const std::string &seed_language)
    : m_wallet(testnet)
{
    bool keys_file_exists;
    bool wallet_file_exists;

    tools::wallet2::wallet_exists(wallet_path, keys_file_exists, wallet_file_exists);

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
    m_wallet.store();
}

Supernode::~Supernode()
{
    m_wallet.store();
    LOG_PRINT_L0("deleting supernode");
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

Supernode *Supernode::createFromViewOnlyWallet(const std::string &path, const std::string &address, const secret_key &viewkey, bool testnet)
{
    Supernode * result = nullptr;
    bool keys_file_exists;
    bool wallet_file_exists;
    // TODO: password
    std::string password = "";
    tools::wallet2::wallet_exists(path, keys_file_exists, wallet_file_exists);

    if (keys_file_exists) {
        LOG_ERROR("attempting to generate view only wallet, but specified file(s) exist. Exiting to not risk overwriting.");
        return result;
    }

    cryptonote::account_public_address wallet_addr;
    if (!cryptonote::get_account_address_from_str(wallet_addr, testnet, address)) {
        LOG_ERROR("Error parsing address");
        return result;
    }

    result = new Supernode(testnet);
    result->m_wallet.generate(path, password, wallet_addr, viewkey);
    return result;
}

Supernode *Supernode::load(const string &wallet_path, const string &wallet_password, const string &daemon_address, bool testnet, const string &seed_language)
{
    Supernode * sn = new Supernode(wallet_path, wallet_password, daemon_address, testnet);

    sn->refresh();
    if (sn->stakeAmount() < Supernode::TIER1_STAKE_AMOUNT) {
       LOG_ERROR("wallet " << sn->walletAddress() << " doesn't have enough stake to be supernode: " << sn->stakeAmount());
       delete sn;
       return nullptr;
    } else {
       LOG_PRINT_L1("Loaded supernode: " << sn->walletAddress() << ", stake: " << sn->stakeAmount());
    }

    return sn;
}

crypto::secret_key Supernode::exportViewkey()
{
    return m_wallet.get_account().get_keys().m_view_secret_key;
}


bool Supernode::signMessage(const std::string &msg, crypto::signature &signature)
{
    if (m_wallet.watch_only()) {
        LOG_ERROR("Attempting to sign with watch-only wallet");
        return false;
    }

    crypto::hash hash;
    crypto::cn_fast_hash(msg.data(), msg.size(), hash);
    const cryptonote::account_keys &keys = m_wallet.get_account().get_keys();
    crypto::generate_signature(hash, keys.m_account_address.m_spend_public_key, keys.m_spend_secret_key, signature);
    return true;
}

bool Supernode::verifySignature(const std::string &msg, const std::string &address, const crypto::signature &signature)
{
    cryptonote::account_public_address wallet_addr;
    if (!cryptonote::get_account_address_from_str(wallet_addr, m_wallet.testnet(), address)) {
        LOG_ERROR("Error parsing address");
        return false;
    }
    crypto::hash hash;
    crypto::cn_fast_hash(msg.data(), msg.size(), hash);
    return crypto::check_signature(hash, wallet_addr.m_spend_public_key, signature);
}



bool Supernode::setDaemonAddress(const std::string &address)
{
    return m_wallet.init(address);
}

bool Supernode::refresh()
{
    m_wallet.refresh();
}

bool Supernode::testnet() const
{
    return m_wallet.testnet();
}

void Supernode::getScoreHash(const crypto::hash &block_hash, crypto::hash &result)
{
    cryptonote::blobdata data = m_wallet.get_account().get_public_address_str(testnet());
    data += epee::string_tools::pod_to_hex(block_hash);
    crypto::cn_fast_hash(data.c_str(), data.size(), result);
}

Supernode::Supernode(bool testnet)
    : m_wallet(testnet)
{

}



} // namespace graft
