
#include "supernode/supernode.h"
#include "lib/graft/requests.h"
#include "supernode/requests.h"
#include "lib/graft/sys_info.h"
#include "supernode/requestdefines.h"
#include "supernode/requests/send_supernode_announce.h"
#include "rta/supernode.h"
#include "rta/fullsupernodelist.h"

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
    return res;
}

void Supernode::prepareSupernodes()
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
                    (stake_wallet_path / m_configEx.stake_wallet_name).string(),
                    "", // TODO
                    m_configEx.cryptonode_rpc_address,
                    m_configEx.common.testnet
                    );

    supernode->setNetworkAddress(m_configEx.http_address + "/dapi/v2.0");

    // create fullsupernode list instance and put it into global context
    graft::FullSupernodeListPtr fsl = boost::make_shared<graft::FullSupernodeList>(
                m_configEx.cryptonode_rpc_address, m_configEx.common.testnet);
    size_t found_wallets = 0;
    MINFO("loading supernodes wallets from: " << watchonly_wallets_path.string());
    size_t loaded_wallets = fsl->loadFromDirThreaded(watchonly_wallets_path.string(), found_wallets);

    if (found_wallets != loaded_wallets) {
        LOG_ERROR("found wallets: " << found_wallets << ", loaded wallets: " << loaded_wallets);
    }
    LOG_PRINT_L0("supernode list loaded");

    // add our supernode as well, it wont be added from announce;
    fsl->add(supernode);

    //put fsl into global context
    Context ctx(getLooper().getGcm());
    ctx.global["supernode"] = supernode;
    ctx.global[CONTEXT_KEY_FULLSUPERNODELIST] = fsl;
    ctx.global["testnet"] = m_configEx.common.testnet;
    ctx.global["watchonly_wallets_path"] = m_configEx.watchonly_wallets_path;
    ctx.global["cryptonode_rpc_address"] = m_configEx.cryptonode_rpc_address;
}

void Supernode::initMisc(ConfigOpts& configOpts)
{
    ConfigOptsEx& coptsex = static_cast<ConfigOptsEx&>(configOpts);
    assert(&m_configEx == &coptsex);
    prepareSupernodes();
    startSupernodePeriodicTasks();
}

void Supernode::startSupernodePeriodicTasks()
{
    // update supernode every interval_ms

    if (m_configEx.stake_wallet_refresh_interval_ms > 0) {
        size_t initial_interval_ms = 1000;
        getLooper().addPeriodicTask(
                    graft::Router::Handler3(nullptr, graft::supernode::request::sendAnnounce, nullptr),
                    std::chrono::milliseconds(m_configEx.stake_wallet_refresh_interval_ms),
                    std::chrono::milliseconds(initial_interval_ms)
                    );
    }
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

