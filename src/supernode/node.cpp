
#include "supernode/node.h"
#include "supernode/server/server.h"
#include "common/utils.h"

#include "requests.h"
#include "sys_info.h"
#include "requestdefines.h"
#include "requests/sendsupernodeannouncerequest.h"
#include "rta/supernode.h"
#include "rta/fullsupernodelist.h"

#include <boost/filesystem.hpp>
#include <boost/property_tree/ini_parser.hpp>

#undef MONERO_DEFAULT_LOG_CATEGORY
#define MONERO_DEFAULT_LOG_CATEGORY "supernode.supernode"

namespace consts {
   static const char*   DATA_PATH = "supernode/data";
   static const char*   STAKE_WALLET_PATH = "stake-wallet";
   static const char*   WATCHONLY_WALLET_PATH = "stake-wallet";
   static const size_t  DEFAULT_STAKE_WALLET_REFRESH_INTERFAL_MS = 5 * 1000;
}

namespace fs = boost::filesystem;

namespace graft::supernode {

using Path = fs::path;

Node::Node(void)
{
}

Node::~Node(void)
{
}

bool Node::run(const Config& cfg)
{
    m_cfg = cfg;
    if(!init(m_cfg))
          return false;

    while(server::RunResult::SignalRestart == Server::run());
    return true;
}

void Node::initMisc(server::Config& srv_cfg)
{
    assert(&m_cfg == &(static_cast<Config&>(srv_cfg)));

    prepareDataDirAndSupernodes();
    startSupernodePeriodicTasks();
}

void Node::prepareDataDirAndSupernodes()
{
    if(m_cfg.data_dir.empty())
    {
        Path p = boost::filesystem::absolute(utils::get_home_dir());
        p /= ".graft/";
        p /= consts::DATA_PATH;
        m_cfg.data_dir = p.string();
    }

    // create data directory if not exists
    Path data_path(m_cfg.data_dir);
    Path stake_wallet_path = data_path / "stake-wallet";
    Path watchonly_wallets_path = data_path / "watch-only-wallets";

    if(!fs::exists(data_path))
    {
        boost::system::error_code ec;
        if(!fs::create_directories(data_path, ec))
            throw std::runtime_error(ec.message());

        if(!fs::create_directories(stake_wallet_path, ec))
            throw std::runtime_error(ec.message());

        if(!fs::create_directories(watchonly_wallets_path, ec))
            throw std::runtime_error(ec.message());
    }

    m_cfg.watchonly_wallets_path = watchonly_wallets_path.string();

    MINFO("data path: " << data_path.string());
    MINFO("stake wallet path: " << stake_wallet_path.string());

    // create supernode instance and put it into global context
    graft::SupernodePtr supernode = boost::make_shared<graft::Supernode>(
                    (stake_wallet_path / m_cfg.stake_wallet_name).string(),
                    "", // TODO
                    m_cfg.cryptonode_rpc_address,
                    m_cfg.testnet
                    );

    supernode->setNetworkAddress(m_cfg.http_address + "/dapi/v2.0");

    // create fullsupernode list instance and put it into global context
    graft::FullSupernodeListPtr fsl = boost::make_shared<graft::FullSupernodeList>(
                m_cfg.cryptonode_rpc_address, m_cfg.testnet);
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
    assert(m_looper);
    Context ctx(m_looper->gcm());
    ctx.global["supernode"] = supernode;
    ctx.global[CONTEXT_KEY_FULLSUPERNODELIST] = fsl;
    ctx.global["testnet"] = m_cfg.testnet;
    ctx.global["watchonly_wallets_path"] = m_cfg.watchonly_wallets_path;
    ctx.global["cryptonode_rpc_address"] = m_cfg.cryptonode_rpc_address;
}

void Node::startSupernodePeriodicTasks()
{
    // update supernode every interval_ms

    if (m_cfg.stake_wallet_refresh_interval_ms > 0) {
        size_t initial_interval_ms = 1000;
        assert(m_looper);
        m_looper->addPeriodicTask(
                    graft::Router::Handler3(nullptr, sendAnnounce, nullptr),
                    std::chrono::milliseconds(m_cfg.stake_wallet_refresh_interval_ms),
                    std::chrono::milliseconds(initial_interval_ms)
                    );
    }
}

void Node::initRouters()
{
    ConnectionManager* httpcm = getConMgr("HTTP");
    setHttpRouters(*httpcm);
    ConnectionManager* coapcm = getConMgr("COAP");
    setCoapRouters(*coapcm);
}

void Node::setHttpRouters(ConnectionManager& httpcm)
{
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
    graft::registerRTARequests(dapi_router);
    httpcm.addRouter(dapi_router);

    Router forward_router;
    graft::registerForwardRequests(forward_router);
    httpcm.addRouter(forward_router);

    Router health_router;
    graft::registerHealthcheckRequests(health_router);
    httpcm.addRouter(health_router);

    Router debug_router;
    graft::registerDebugRequests(debug_router);
    httpcm.addRouter(debug_router);
}

void Node::setCoapRouters(ConnectionManager& coapcm)
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

}

