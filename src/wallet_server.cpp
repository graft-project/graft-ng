#include "wallet_server.h"
#include "backtrace.h"
#include "requests.h"
#include "requestdefines.h"

#include <boost/program_options.hpp>
#include <boost/property_tree/ini_parser.hpp>

const int WALLET_DISK_CACHES_UPDATE_TIME_MS = 60 * 1000; //TODO: move to config

namespace po = boost::program_options;

namespace graft {

WalletServer::WalletServer()
{
}

WalletServer::~WalletServer()
{
}

void WalletServer::registerWalletRequests(ConnectionManager& httpcm)
{
    Router router;

    graft::registerWalletRequests(router, *m_walletManager);

    httpcm.addRouter(router);
}

void WalletServer::setHttpRouters(ConnectionManager& httpcm)
{
    Router dapi_router("/dapi/v2.0");
    auto http_test = [](const Router::vars_t&, const Input&, Context&, Output&)->Status
    {
        std::cout << "blah-blah" << std::endl;
        return Status::Ok;
    };

    Router health_router;
    graft::registerHealthcheckRequests(health_router);
    httpcm.addRouter(health_router);

    registerWalletRequests(httpcm);
}

void WalletServer::initMisc(ConfigOpts& configOpts)
{
    assert(m_looper);

    Context ctx(m_looper->getGcm());

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
    assert(m_looper);

    m_walletManager.reset(new WalletManager(*m_looper, m_configEx.testnet));
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
    assert(m_looper);

    Router::Handler flush_caches_handler = [this](const graft::Router::vars_t&, const graft::Input&, graft::Context&, graft::Output&) {
        flushWalletDiskCaches();
        return Status::Ok;
    };
 
    m_looper->addPeriodicTask(graft::Router::Handler3(nullptr, flush_caches_handler, nullptr),
        std::chrono::milliseconds(WALLET_DISK_CACHES_UPDATE_TIME_MS), std::chrono::milliseconds(WALLET_DISK_CACHES_UPDATE_TIME_MS));
}

}//namespace graft
