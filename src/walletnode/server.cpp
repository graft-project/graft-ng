#include "walletnode/server.h"
#include "lib/graft/backtrace.h"
#include "walletnode/requests.h"
#include "supernode/requestdefines.h"

#include <boost/program_options.hpp>
#include <boost/property_tree/ini_parser.hpp>

const int WALLET_DISK_CACHES_UPDATE_TIME_MS = 10 * 60 * 1000; //TODO: move to config

namespace po = boost::program_options;

namespace graft {

namespace walletnode {

WalletServer::WalletServer()
{
}

WalletServer::~WalletServer()
{
}

void WalletServer::registerWalletRequests(ConnectionManager& httpcm)
{
    Router router;

    graft::walletnode::request::registerWalletRequests(router, *m_walletManager);

    httpcm.addRouter(router);
}

void WalletServer::setHttpRouters(ConnectionManager& httpcm)
{
    Router health_router;
    graft::request::registerHealthcheckRequests(health_router);
    httpcm.addRouter(health_router);

    registerWalletRequests(httpcm);
}

void WalletServer::initMisc(ConfigOpts& configOpts)
{
    Context ctx(getLooper().getGcm());

    ctx.global["testnet"] = m_configEx.testnet;
    ctx.global["cryptonode_rpc_address"] = m_configEx.cryptonode_rpc_address;

    initWalletManager();

    startPeriodicTasks();
}

bool WalletServer::run(int argc, const char** argv)
{
    for(;;)
    {
        if(!init(argc, argv, m_configEx))
            return false;

        argc = 1;

        if(GraftServer::run() != RunRes::SignalRestart)
            break;
    }

    return true;
}

bool WalletServer::initConfigOption(int argc, const char** argv, ConfigOpts& configOpts)
{
    if (!GraftServer::initConfigOption(argc, argv, configOpts))
        return false;

    boost::property_tree::ptree config;
    boost::property_tree::ini_parser::read_ini(m_configEx.config_filename, config);

    const boost::property_tree::ptree& server_conf = config.get_child("server");

    m_configEx.testnet = server_conf.get<bool>("testnet", false);

    return true;
}

void WalletServer::initWalletManager()
{
    assert(!m_walletManager);

    m_walletManager = std::make_unique<WalletManager>(getLooper(), m_configEx.testnet);
}

void WalletServer::initRouters()
{
    ConnectionManager* httpcm = getConMgr("HTTP");
    setHttpRouters(*httpcm);
}

void WalletServer::flushWalletDiskCaches()
{
    assert(&*m_walletManager);
    m_walletManager->flushDiskCaches();
}

void WalletServer::startPeriodicTasks()
{
    Router::Handler flush_caches_handler = [this](const graft::Router::vars_t&, const graft::Input&, graft::Context&, graft::Output&) {
        flushWalletDiskCaches();
        return Status::Ok;
    };
 
    getLooper().addPeriodicTask(graft::Router::Handler3(nullptr, flush_caches_handler, nullptr),
        std::chrono::milliseconds(WALLET_DISK_CACHES_UPDATE_TIME_MS), std::chrono::milliseconds(1));
}

}//namespace walletnode
}//namespace graft
