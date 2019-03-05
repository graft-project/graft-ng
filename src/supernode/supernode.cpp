
#include "supernode/supernode.h"
#include "lib/graft/requests.h"
#include "supernode/requests.h"
#include "lib/graft/sys_info.h"
#include "supernode/requestdefines.h"
#include "supernode/requests/send_supernode_announce.h"
#include "supernode/requests/redirect.h"
#include "rta/supernode.h"
#include "rta/fullsupernodelist.h"
#include "lib/graft/graft_exception.h"

#include <boost/property_tree/ini_parser.hpp>

#undef MONERO_DEFAULT_LOG_CATEGORY
#define MONERO_DEFAULT_LOG_CATEGORY "supernode.supernode"

namespace consts {
   static const char * STAKE_WALLET_PATH = "stake-wallet";
   static const char * WATCHONLY_WALLET_PATH = "stake-wallet";
   static const size_t DEFAULT_STAKE_WALLET_REFRESH_INTERFAL_MS = 5 * 1000;
}

namespace graft
{
namespace snd
{

namespace
{

std::string exec(const std::string& cmd)
{
    std::array<char, 16> buffer;
    std::string result;
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd.data(), "r"), pclose);
    if (!pipe) {
        throw std::runtime_error("popen() failed!");
    }
    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        result += buffer.data();
    }
    return result;
}

}//namespace

bool Supernode::initConfigOption(int argc, const char** argv, ConfigOpts& configOpts)
{
    bool res = GraftServer::initConfigOption(argc, argv, configOpts);
    if(!res) return res;

    ConfigOptsEx& coptsex = static_cast<ConfigOptsEx&>(configOpts);
    assert(&m_configEx == &coptsex);

    boost::property_tree::ptree config;
    boost::property_tree::ini_parser::read_ini(m_configEx.config_filename, config);

    const boost::property_tree::ptree& server_conf = config.get_child("server");
    m_configEx.stake_wallet_name = server_conf.get<std::string>("stake-wallet-name", "stake-wallet");
    m_configEx.stake_wallet_refresh_interval_ms = server_conf.get<size_t>("stake-wallet-refresh-interval-ms",
                                                                      consts::DEFAULT_STAKE_WALLET_REFRESH_INTERFAL_MS);
    m_configEx.stake_wallet_refresh_interval_random_factor = server_conf.get<double>("stake-wallet-refresh-interval-random-factor", 0);

    {//get external address
        //try get from [stun]
        boost::optional<boost::property_tree::ptree&> stun_conf = config.get_child_optional("stun");
        bool ok = !!stun_conf;
        if(ok)
        {
            bool stun_enabled = stun_conf.get().get<bool>("enabled", false);
            ok = stun_enabled;
        }
        if(ok)
        {//using stun
            std::string server = stun_conf.get().get<std::string>("server", "");
            std::string port = stun_conf.get().get<std::string>("port", "");
            std::string cmd = stun_conf.get().get<std::string>("cmd", "");

            std::string env = "stun_server=" + server + " " + "stun_port=" + port + "; ";
            std::string external_address = exec(env + cmd);
            if(external_address.empty())
            {
                throw graft::exit_error("Obtaining external IP address using the stun has failed. Maybe you should install 'stuntman-client' package and correct parameters. Or disable the stun and set external-address manually.");
            }
            assert(!m_configEx.http_address.empty());
            auto pos = m_configEx.http_address.find(':');
            if(pos != std::string::npos)
            {
                external_address += ":" + m_configEx.http_address.substr(pos);
            }
            m_configEx.external_address = external_address;
        }
        else
        {//external-address or http-address
            m_configEx.external_address = server_conf.get<std::string>("external-address", "");
            if(m_configEx.external_address.empty())
            {
                assert(!m_configEx.http_address.empty());
                m_configEx.external_address = m_configEx.http_address;
            }
        }
    }

    if(m_configEx.common.wallet_public_address.empty())
    {
        throw graft::exit_error("Configuration parameter 'wallet-public-address' cannot be empty.");
    }
    m_configEx.jump_node_coefficient = server_conf.get<double>("jump-node-coefficient", .3);
    if(m_configEx.jump_node_coefficient < .0001 || 1.0001 < m_configEx.jump_node_coefficient)
    {
        throw graft::exit_error("invalid value of jump-node-coefficient.");
    }
    m_configEx.redirect_timeout_ms = server_conf.get<uint32_t>("redirect-timeout-ms", 50*60*1000);
    return res;
}

void Supernode::prepareSupernode()
{
    // create data directory if not exists
    boost::filesystem::path data_path(m_configEx.common.data_dir);
    boost::filesystem::path stake_wallet_path = data_path / "stake-wallet";
    boost::filesystem::path watchonly_wallets_path = data_path / "watch-only-wallets";

    if (!boost::filesystem::exists(data_path)) {
        boost::system::error_code ec;
        if (!boost::filesystem::create_directories(data_path, ec)) {
            throw std::runtime_error(ec.message());
        }

        if (!boost::filesystem::create_directories(stake_wallet_path, ec)) {
            throw std::runtime_error(ec.message());
        }

        if (!boost::filesystem::create_directories(watchonly_wallets_path, ec)) {
            throw std::runtime_error(ec.message());
        }
    }

    m_configEx.watchonly_wallets_path = watchonly_wallets_path.string();

    MINFO("data path: " << data_path.string());
    MINFO("stake wallet path: " << stake_wallet_path.string());

    // create supernode instance and put it into global context
    graft::SupernodePtr supernode = boost::make_shared<graft::Supernode>(
                    m_configEx.common.wallet_public_address,
                    crypto::public_key(),
                    m_configEx.cryptonode_rpc_address,
                    m_configEx.common.testnet
                    );
    std::string keyfilename = (data_path / "supernode.keys").string();
    if (!supernode->loadKeys(keyfilename)) {
        // supernode is not initialized, generating key
        supernode->initKeys();
        if (!supernode->saveKeys(keyfilename)) {
            MERROR("Failed to save keys");
            throw std::runtime_error("Failed to save keys");
        }
    }

    supernode->setNetworkAddress(m_configEx.http_address + "/dapi/v2.0");

    // create fullsupernode list instance and put it into global context
    graft::FullSupernodeListPtr fsl = boost::make_shared<graft::FullSupernodeList>(
                m_configEx.cryptonode_rpc_address, m_configEx.common.testnet);
    fsl->add(supernode);

    //put fsl into global context
    Context ctx(getLooper().getGcm());
    ctx.global[CONTEXT_KEY_SUPERNODE] = supernode;
    ctx.global[CONTEXT_KEY_FULLSUPERNODELIST] = fsl;
    ctx.global["testnet"] = m_configEx.common.testnet;
    ctx.global["watchonly_wallets_path"] = m_configEx.watchonly_wallets_path;
    ctx.global["cryptonode_rpc_address"] = m_configEx.cryptonode_rpc_address;
    ctx.global["supernode_url"] = m_configEx.http_address + "/dapi/v2.0";
    ctx.global["external_address"] = m_configEx.external_address;
    ctx.global["jump_node_coefficient"] = m_configEx.jump_node_coefficient;
    ctx.global["redirect_timeout_ms"] = m_configEx.redirect_timeout_ms;
}

void Supernode::initMisc(ConfigOpts& configOpts)
{
    ConfigOptsEx& coptsex = static_cast<ConfigOptsEx&>(configOpts);
    assert(&m_configEx == &coptsex);

    prepareSupernode();
    startSupernodePeriodicTasks();

    std::chrono::milliseconds duration( 5000 );
    auto deferred_task = [duration, this] () { std::this_thread::sleep_for(duration); this->loadStakeWallets(); };
    std::thread t(deferred_task);
    t.detach();
}

void Supernode::startSupernodePeriodicTasks()
{
    // update supernode every interval_ms

    if (m_configEx.stake_wallet_refresh_interval_ms > 0) {
        size_t initial_interval_ms = 1000;
/*
        getLooper().addPeriodicTask(
                    graft::Router::Handler3(nullptr, graft::supernode::request::sendAnnounce, nullptr),
                    std::chrono::milliseconds(m_configEx.stake_wallet_refresh_interval_ms),
                    std::chrono::milliseconds(initial_interval_ms),
                    m_configEx.stake_wallet_refresh_interval_random_factor
                    );
*/
        getLooper().addPeriodicTask(
                    graft::Router::Handler3(nullptr, graft::supernode::request::periodicRegisterSupernode, nullptr),
#if tst
                    std::chrono::milliseconds(5*initial_interval_ms),
#else
                    std::chrono::milliseconds(m_configEx.stake_wallet_refresh_interval_ms),
#endif
                    std::chrono::milliseconds(initial_interval_ms),
                    m_configEx.stake_wallet_refresh_interval_random_factor
                    );
        getLooper().addPeriodicTask(
                    graft::Router::Handler3(nullptr, graft::supernode::request::periodicUpdateRedirectIds, nullptr),
#if tst
                    std::chrono::milliseconds(10*initial_interval_ms),
#else
                    std::chrono::milliseconds(m_configEx.stake_wallet_refresh_interval_ms),
#endif
                    std::chrono::milliseconds(initial_interval_ms),
                    m_configEx.stake_wallet_refresh_interval_random_factor
                    );
#if tst
        getLooper().addPeriodicTask(
                    graft::Router::Handler3(nullptr, graft::supernode::request::test_startBroadcast, nullptr),
                    std::chrono::milliseconds(15*initial_interval_ms),
                    std::chrono::milliseconds(initial_interval_ms),
                    m_configEx.stake_wallet_refresh_interval_random_factor
                    );
#endif
    }

    // sync with cryptonode

    auto handler = [](const graft::Router::vars_t& vars, const graft::Input& input, graft::Context& ctx, graft::Output& output)->graft::Status
    {
        graft::SupernodePtr supernode = ctx.global.get(CONTEXT_KEY_SUPERNODE, graft::SupernodePtr(nullptr));

        if (!supernode.get()) {
            LOG_ERROR("supernode is not set in global context");
            return graft::Status::Error;
        }

        if (FullSupernodeListPtr fsl = ctx.global.get(CONTEXT_KEY_FULLSUPERNODELIST, FullSupernodeListPtr()))
        {
            fsl->synchronizeWithCryptonode(supernode->networkAddress().c_str(), supernode->idKeyAsString().c_str());
        }

        return graft::Status::Ok;
    };

    static const size_t CRYPTONODE_SYNCHRONIZATION_PERIOD_MS = 1000;

    getConnectionBase().getLooper().addPeriodicTask(
                graft::Router::Handler3(nullptr, handler, nullptr),
                std::chrono::milliseconds(CRYPTONODE_SYNCHRONIZATION_PERIOD_MS)
                );
}

void Supernode::setHttpRouters(ConnectionManager& httpcm)
{
    using namespace graft::supernode::request;

    Router dapi_router("/dapi/v2.0");
    auto http_test = [](const Router::vars_t&, const Input&, Context&, Output&)->Status
    {
        std::cout << "blah-blah" << std::endl;
        return Status::Ok;
    };
    // Router::Handler3 h3_test1(http_test, nullptr, nullptr);

    // dapi_router.addRoute("/test", METHOD_GET, h3_test1);
    // httpcm.addRouter(dapi_router);

    // Router http_router;
    registerRTARequests(dapi_router);
    httpcm.addRouter(dapi_router);

    Router walletapi_router("/walletapi");
    registerWalletApiRequests(walletapi_router);
    httpcm.addRouter(walletapi_router);

    Router forward_router;
    registerForwardRequests(forward_router);
    httpcm.addRouter(forward_router);

    Router health_router;
    graft::request::registerHealthcheckRequests(health_router);
    httpcm.addRouter(health_router);

    Router debug_router;
    registerDebugRequests(debug_router);
    httpcm.addRouter(debug_router);
}

void Supernode::setCoapRouters(ConnectionManager& coapcm)
{
    Router coap_router("/coap");
    auto coap_test = [](const Router::vars_t&, const Input&, Context&, Output&)->Status
    {
        std::cout << "blah" << std::endl;
        return Status::Ok;
    };
    Router::Handler3 h3_test(coap_test, nullptr, nullptr);

    coap_router.addRoute("/test", METHOD_GET, h3_test);
    coap_router.addRoute("/test1", METHOD_GET, h3_test);
    coap_router.addRoute("/test2", METHOD_GET, h3_test);

    coapcm.addRouter(coap_router);
}

void Supernode::loadStakeWallets()
{

    Context ctx(getLooper().getGcm());
    std::string watchonly_wallets_path = ctx.global.get("watchonly_wallets_path", std::string());

    FullSupernodeListPtr fsl = ctx.global.get(CONTEXT_KEY_FULLSUPERNODELIST, FullSupernodeListPtr());
    size_t found_wallets = 0;
    MDEBUG("loading supernodes wallets from: " << watchonly_wallets_path);
    size_t loaded_wallets = fsl->loadFromDirThreaded(watchonly_wallets_path, found_wallets);

    if (found_wallets != loaded_wallets) {
        LOG_ERROR("found wallets: " << found_wallets << ", loaded wallets: " << loaded_wallets);
    }
    LOG_PRINT_L0("supernode list loaded");

    // add our supernode as well, it wont be added from announce;
}

void Supernode::initRouters()
{
    ConnectionManager* httpcm = getConMgr("HTTP");
    setHttpRouters(*httpcm);
    ConnectionManager* coapcm = getConMgr("COAP");
    setCoapRouters(*coapcm);
}

bool Supernode::run(int argc, const char** argv)
{
    for(bool run = true; run;)
    {
        run = false;
        if (!init(argc, argv, m_configEx)) {
            // TODO: explain reason?
            MERROR("Failed to initialize supernode");
            return false;
        }
        argc = 1;
        RunRes res = GraftServer::run();
        if(res == RunRes::SignalRestart) run = true;
    }
    return true;
}

} }

