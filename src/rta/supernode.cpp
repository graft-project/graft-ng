#include "supernode.h"
#include "fullsupernodelist.h"

#include <misc_log_ex.h>
#include <cryptonote_basic/cryptonote_basic_impl.h>
#include <boost/filesystem.hpp>
#include <iostream>

using namespace std;

namespace graft {

Supernode::Supernode(const string &wallet_path, const string &wallet_password, const string &daemon_address, bool testnet,
                     const string &seed_language)
    : m_wallet(testnet)
{
    bool keys_file_exists;
    bool wallet_file_exists;

    tools::wallet2::wallet_exists(wallet_path, keys_file_exists, wallet_file_exists);

    LOG_PRINT_L3("keys_file_exists: " << boolalpha << keys_file_exists << noboolalpha
                 << "  wallet_file_exists: " << boolalpha << wallet_file_exists << noboolalpha);

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
    m_wallet.init(daemon_address);
    m_wallet.store();
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

bool Supernode::exportKeyImages(vector<Supernode::SignedKeyImage> &key_images) const
{
    key_images = m_wallet.export_key_images();
    return !key_images.empty();
}

bool Supernode::importKeyImages(const vector<Supernode::SignedKeyImage> &key_images, uint64_t &height)
{
    uint64_t spent = 0, unspent = 0;
    height = m_wallet.import_key_images(key_images, spent, unspent);
    return height > 0;
}

Supernode *Supernode::createFromViewOnlyWallet(const string &path, const string &address, const secret_key &viewkey, bool testnet)
{
    Supernode * result = nullptr;
    bool keys_file_exists;
    bool wallet_file_exists;
    // TODO: password
    string password = "";
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
    if (false/*sn->stakeAmount() < Supernode::TIER1_STAKE_AMOUNT*/) {
       LOG_ERROR("wallet " << sn->walletAddress() << " doesn't have enough stake to be supernode: " << sn->stakeAmount());
       delete sn;
       return nullptr;
    } else {
       LOG_PRINT_L1("Loaded supernode: " << sn->walletAddress() << ", stake: " << sn->stakeAmount());
    }

    return sn;
}

bool Supernode::updateFromAnnounce(const SupernodeAnnounce &announce)
{
    // check if address match
    if (this->walletAddress() != announce.address) {
        LOG_ERROR("wrong address. this address: " << this->walletAddress() << ", announce address: " << announce.address);
        return false;
    }

    vector<SignedKeyImage> signed_key_images;

    for (const SignedKeyImageStr &skis : announce.signed_key_images) {
        crypto::key_image ki;
        crypto::signature s;

        if (!epee::string_tools::hex_to_pod(skis.key_image, ki)) {
            LOG_ERROR("failed to parse key image: " << skis.key_image);
            return false;
        }

        if (!epee::string_tools::hex_to_pod(skis.signature, s)) {
            LOG_ERROR("failed to parse key signature: " << skis.signature);
            return false;
        }
        signed_key_images.push_back(std::make_pair(ki, s));
    }

    uint64_t height = 0;
    if (!importKeyImages(signed_key_images, height)) {
        LOG_ERROR("failed to import key images");
        return false;
    }

    if (!signed_key_images.empty() && height == 0) {
        LOG_ERROR("key images imported but height is 0");
        return false;
    }
    // TODO: check self amount vs announced amount
    setNetworkAddress(announce.network_address);
    return true;

}

Supernode *Supernode::createFromAnnounce(const string &path, const SupernodeAnnounce &announce, const std::string &daemon_address,
                                         bool testnet)
{
    crypto::secret_key viewkey;
    if (!epee::string_tools::hex_to_pod(announce.secret_viewkey, viewkey)) {
        LOG_ERROR("Failed to parse secret viewkey from string: " << announce.secret_viewkey);
        return false;
    }

    Supernode * result = nullptr;
    try {
        result = Supernode::createFromViewOnlyWallet(path, announce.address, viewkey, testnet);


        // XXX before importing key images, wallet needs to be connected to daemon and syncrhonized
        if (result) {

            result->setDaemonAddress(daemon_address);
            result->refresh();
            result->updateFromAnnounce(announce);

        }

    } catch (...) {
        LOG_ERROR("wallet exception");
        delete result;
        result = nullptr;
    }


    return result;
}

bool Supernode::prepareAnnounce(SupernodeAnnounce &announce)
{
    announce.timestamp = time(nullptr);
    announce.secret_viewkey = epee::string_tools::pod_to_hex(this->exportViewkey());
    announce.height = m_wallet.get_blockchain_current_height();

    vector<Supernode::SignedKeyImage> signed_key_images;
    if (!exportKeyImages(signed_key_images)) {
        LOG_ERROR("Failed to export key images");
        return false;
    }

    for (const SignedKeyImage &ski : signed_key_images) {
        SignedKeyImageStr skis;
        skis.key_image = epee::string_tools::pod_to_hex(ski.first);
        skis.signature = epee::string_tools::pod_to_hex(ski.second);
        announce.signed_key_images.push_back(skis);
    }

    announce.stake_amount = this->stakeAmount();
    announce.address =  this->walletAddress();
    announce.network_address = this->networkAddress();

    return true;
}

crypto::secret_key Supernode::exportViewkey() const
{
    return m_wallet.get_account().get_keys().m_view_secret_key;
}


bool Supernode::signMessage(const string &msg, crypto::signature &signature) const
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

bool Supernode::verifySignature(const string &msg, const string &address, const crypto::signature &signature) const
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



bool Supernode::setDaemonAddress(const string &address)
{
    return m_wallet.init(address);
}

void Supernode::refresh()
{
   m_wallet.refresh();
}

bool Supernode::testnet() const
{
    return m_wallet.testnet();
}

void Supernode::getScoreHash(const crypto::hash &block_hash, crypto::hash &result) const
{
    cryptonote::blobdata data = m_wallet.get_account().get_public_address_str(testnet());
    data += epee::string_tools::pod_to_hex(block_hash);
    crypto::cn_fast_hash(data.c_str(), data.size(), result);
}

string Supernode::networkAddress() const
{
    return m_network_address;
}

void Supernode::setNetworkAddress(const string &networkAddress)
{
    if (m_network_address != networkAddress)
        m_network_address = networkAddress;
}

Supernode::Supernode(bool testnet)
    : m_wallet(testnet)
{

}



} // namespace graft
