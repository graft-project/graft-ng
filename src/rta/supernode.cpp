#include "supernode/supernode.h"
#include "rta/fullsupernodelist.h"
#include "supernode/requests/send_supernode_announce.h"

#include <misc_log_ex.h>
#include <wallet/wallet2.h>
#include <cryptonote_basic/cryptonote_basic_impl.h>
#include <boost/filesystem.hpp>
#include <boost/thread/locks.hpp>
#include <iostream>
#include <ctime>

#undef MONERO_DEFAULT_LOG_CATEGORY
#define MONERO_DEFAULT_LOG_CATEGORY "supernode.supernode"

using namespace std;

namespace graft {


using graft::supernode::request::SignedKeyImageStr;
using graft::supernode::request::SupernodeAnnounce;

boost::shared_ptr<boost::asio::io_service> Supernode::m_ioservice { new boost::asio::io_service() };

#ifndef __cpp_inline_variables
constexpr uint64_t Supernode::TIER1_STAKE_AMOUNT, Supernode::TIER2_STAKE_AMOUNT, Supernode::TIER3_STAKE_AMOUNT, Supernode::TIER4_STAKE_AMOUNT;
#endif

Supernode::Supernode(const string &wallet_path, const string &wallet_password, const string &daemon_address, bool testnet,
                     const string &seed_language)
    : m_wallet{new tools::wallet2(testnet, false, Supernode::m_ioservice)}
    , m_last_update_time {0}
{
    bool keys_file_exists;
    bool wallet_file_exists;

    tools::wallet2::wallet_exists(wallet_path, keys_file_exists, wallet_file_exists);

    MDEBUG("daemon address: " << daemon_address << ", keys_file_exists: " << boolalpha << keys_file_exists << noboolalpha
                 << "  wallet_file_exists: " << boolalpha << wallet_file_exists << noboolalpha);

    // wallet needs to be initialized with the daemon address first.
    // Otherwize, wallet will get wrong "refresh_from_blockheight" value
    m_wallet->init(daemon_address);

    // existing wallet, open it
    if (keys_file_exists) {
        m_wallet->load(wallet_path, wallet_password);
    // new wallet, generating it
    } else {
        if(!seed_language.empty())
            m_wallet->set_seed_language(seed_language);
        crypto::secret_key recovery_val, secret_key;
        recovery_val = m_wallet->generate(wallet_path, wallet_password, secret_key, false, false);
    }

    // new wallet. in case we don't have a connection to a daemon at the moment wallet created
    // it may happen wallet get "refresh from block height" parameter in the future
    if (!keys_file_exists) {
        std::string error;
        uint64 daemon_height = m_wallet->get_daemon_blockchain_height(error);
        // daemon_height will be 0 in case no daemon
        if (daemon_height == 0)
            m_wallet->set_refresh_from_block_height(0);
    }
    m_wallet->store();
    MDEBUG("refresh from block height: " << m_wallet->get_refresh_from_block_height());
    MINFO("supernode created: " << "[" << this << "] " <<  this->walletAddress());
}

Supernode::~Supernode()
{
    LOG_PRINT_L0("destroying supernode: " << "[" << this << "] " <<  this->walletAddress());
    {
        boost::unique_lock<boost::shared_mutex> writeLock(m_wallet_guard);
        m_wallet->store();
    }
}

uint64_t Supernode::stakeAmount() const
{
    boost::shared_lock<boost::shared_mutex> readLock(m_wallet_guard);
    return m_wallet->unspent_balance();
}

uint32_t Supernode::tier() const
{
    auto stake = stakeAmount();
    return 0 +
        (stake >= TIER1_STAKE_AMOUNT) +
        (stake >= TIER2_STAKE_AMOUNT) +
        (stake >= TIER3_STAKE_AMOUNT) +
        (stake >= TIER4_STAKE_AMOUNT);
}

uint64_t Supernode::walletBalance() const
{
    boost::shared_lock<boost::shared_mutex> readLock(m_wallet_guard);
    return m_wallet->balance();
}

string Supernode::walletAddress() const
{
    return m_wallet->get_account().get_public_address_str(m_wallet->testnet());
}

uint64_t Supernode::daemonHeight() const
{
    uint64_t result = 0;
    std::string err;
    boost::shared_lock<boost::shared_mutex> readLock(m_wallet_guard);
    result = m_wallet->get_daemon_blockchain_height(err);
    if (!result) {
        LOG_ERROR(err);
    }
    return result;
}

bool Supernode::exportKeyImages(vector<Supernode::SignedKeyImage> &key_images) const
{
    boost::shared_lock<boost::shared_mutex> readLock(m_wallet_guard);
    try {
        key_images = m_wallet->export_key_images();
        return !key_images.empty();
    } catch (const std::exception &e) {
        LOG_ERROR("wallet exception: " << e.what());
        return false;
    } catch (...) {
        LOG_ERROR("unknown exception");
        return false;
    }
}

bool Supernode::importKeyImages(const vector<Supernode::SignedKeyImage> &key_images, uint64_t &height)
{
    uint64_t spent = 0;
    uint64_t unspent = 0;

    try
    {
        // shared_mutex isn't recursive, we need to unlock it before daemonHeight() called
        boost::unique_lock<boost::shared_mutex> writeLock(m_wallet_guard);
        m_wallet->import_key_images(key_images, spent, unspent);
        m_last_update_time  = static_cast<uint64_t>(std::time(nullptr));
    }
    catch(const std::exception& e)
    {
        LOG_ERROR("wallet exception: " << e.what());
        return false;
    }
    catch (...)
    {
        LOG_ERROR("unknown exception");
        return false;
    }

    height = this->daemonHeight();
    return true;
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
    result->m_wallet->generate(path, password, wallet_addr, viewkey);
    return result;
}

Supernode *Supernode::load(const string &wallet_path, const string &wallet_password, const string &daemon_address, bool testnet, const string &seed_language)
{
    Supernode * sn = nullptr;
    try {
        sn = new Supernode(wallet_path, wallet_password, daemon_address, testnet);
        sn->refresh();

        if (false/*sn->stakeAmount() < Supernode::TIER1_STAKE_AMOUNT*/) {
            LOG_ERROR("wallet " << sn->walletAddress() << " doesn't have enough stake to be supernode: " << sn->stakeAmount());
            delete sn;
            return nullptr;
        } else {
            LOG_PRINT_L1("Loaded supernode: " << sn->walletAddress() << ", stake: " << sn->stakeAmount());
        }
    } catch (...) { // wallet exception; TODO: catch specific exception if possible
        LOG_ERROR("libwallet exception");
    }

    return sn;
}

bool Supernode::updateFromAnnounce(const SupernodeAnnounce &announce)
{

    // check if address match
    MDEBUG("updating supernode from announce: " << announce.address);
    if (this->walletAddress() != announce.address) {
        LOG_ERROR("wrong address. this address: " << this->walletAddress() << ", announce address: " << announce.address);
        return false;
    }

    // update wallet's blockchain first
    // refresh is protected by mutex
    if (!this->refresh())
        return false;

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
    m_last_update_time  = static_cast<int64_t>(std::time(nullptr));
    uint64 stake_amount = stakeAmount();
    MDEBUG("update from announce done for: " << walletAddress() <<
            "; last update time updated to: " << m_last_update_time <<
            "; stake amount: " << stake_amount);
    return true;
}

Supernode *Supernode::createFromAnnounce(const string &path, const SupernodeAnnounce &announce, const std::string &daemon_address,
                                         bool testnet)
{
    Supernode * result = nullptr;

    crypto::secret_key viewkey;
    if (!epee::string_tools::hex_to_pod(announce.secret_viewkey, viewkey)) {
        LOG_ERROR("Failed to parse secret viewkey from string: " << announce.secret_viewkey);
        return nullptr;
    }

    try {
        result = Supernode::createFromViewOnlyWallet(path, announce.address, viewkey, testnet);
        // before importing key images, wallet needs to be connected to daemon and syncrhonized
        if (result) {
            result->setDaemonAddress(daemon_address);
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
    {
        boost::shared_lock<boost::shared_mutex> lock(m_wallet_guard);
        if (!m_wallet->is_synced()) {
            MWARNING("Wallet is not synced");
            return false;
        }

        announce.timestamp = time(nullptr);
        announce.secret_viewkey = epee::string_tools::pod_to_hex(this->exportViewkey());
        announce.height = m_wallet->get_blockchain_current_height();

        vector<Supernode::SignedKeyImage> signed_key_images;
        if (!exportKeyImages(signed_key_images)) {
            LOG_ERROR("Failed to export key images");
            return false;
        }

        for (const SignedKeyImage &ski : signed_key_images) {
            SignedKeyImageStr skis;
            skis.key_image = epee::string_tools::pod_to_hex(ski.first);
            skis.signature = epee::string_tools::pod_to_hex(ski.second);
            announce.signed_key_images.push_back(std::move(skis));
        }
    }

    announce.stake_amount = this->stakeAmount();
    announce.address =  this->walletAddress();
    announce.network_address = this->networkAddress();

    return true;
}

crypto::secret_key Supernode::exportViewkey() const
{
    boost::shared_lock<boost::shared_mutex> readLock(m_wallet_guard);
    return m_wallet->get_account().get_keys().m_view_secret_key;
}


bool Supernode::signMessage(const string &msg, crypto::signature &signature) const
{
    if (m_wallet->watch_only()) {
        LOG_ERROR("Attempting to sign with watch-only wallet");
        return false;
    }

    crypto::hash hash;
    crypto::cn_fast_hash(msg.data(), msg.size(), hash);
    return this->signHash(hash, signature);
}

bool Supernode::signHash(const crypto::hash &hash, crypto::signature &signature) const
{
    const cryptonote::account_keys &keys = m_wallet->get_account().get_keys();
    crypto::generate_signature(hash, keys.m_account_address.m_spend_public_key, keys.m_spend_secret_key, signature);
    return true;
}

bool Supernode::verifySignature(const string &msg, const string &address, const crypto::signature &signature) const
{
    crypto::hash hash;
    crypto::cn_fast_hash(msg.data(), msg.size(), hash);
    return verifyHash(hash, address, signature);
}

bool Supernode::verifyHash(const crypto::hash &hash, const string &address, const crypto::signature &signature) const
{
    cryptonote::account_public_address wallet_addr;
    if (!cryptonote::get_account_address_from_str(wallet_addr, m_wallet->testnet(), address)) {
        LOG_ERROR("Error parsing address");
        return false;
    }
    return crypto::check_signature(hash, wallet_addr.m_spend_public_key, signature);
}



bool Supernode::setDaemonAddress(const string &address)
{
    // boost::unique_lock<boost::shared_mutex> writeLock(m_wallet_guard);
    return m_wallet->init(address);
}

bool Supernode::refresh()
{
    MDEBUG("about to refresh account: " << this->walletAddress());
    boost::unique_lock<boost::shared_mutex> writeLock(m_wallet_guard);
    try {
        m_wallet->refresh();
        m_wallet->rescan_unspent();
        m_wallet->store();
    } catch (...) {
        LOG_ERROR("Failed to refresh supernode wallet: " << this->walletAddress());
        return false;
    }
    MDEBUG("account refreshed: " << this->walletAddress());
    return true;
}

bool Supernode::testnet() const
{
    return m_wallet->testnet();
}

void Supernode::getScoreHash(const crypto::hash &block_hash, crypto::hash &result) const
{
    // address wont be changed during object lifetime, not need to lock
    cryptonote::blobdata data = m_wallet->get_account().get_public_address_str(testnet());
    data += epee::string_tools::pod_to_hex(block_hash);
    crypto::cn_fast_hash(data.c_str(), data.size(), result);
}

string Supernode::networkAddress() const
{
    // boost::shared_lock<boost::shared_mutex> readLock(m_wallet_guard);
    return m_network_address;
}

void Supernode::setNetworkAddress(const string &networkAddress)
{
    if (m_network_address != networkAddress)
        m_network_address = networkAddress;
}

bool Supernode::getAmountFromTx(const cryptonote::transaction &tx, uint64_t &amount)
{
    return m_wallet->get_amount_from_tx(tx, amount);
}

bool Supernode::getPaymentIdFromTx(const cryptonote::transaction &tx, string &paymentId)
{
    return true;
}

bool Supernode::validateAddress(const string &address, bool testnet)
{
    cryptonote::account_public_address acc = AUTO_VAL_INIT(acc);
    return address.size() > 0 && cryptonote::get_account_address_from_str(acc, testnet, address);
}

int64_t Supernode::lastUpdateTime() const
{

    return m_last_update_time;
}

void Supernode::setLastUpdateTime(int64_t time)
{
    m_last_update_time.store(time);
}

bool Supernode::busy() const
{
    if (m_wallet_guard.try_lock_shared()) {
        m_wallet_guard.unlock_shared();
        return false;
    } else {
        return true;
    }
}

Supernode::Supernode(bool testnet)
: m_wallet{ new tools::wallet2(testnet) }
{

}

}

