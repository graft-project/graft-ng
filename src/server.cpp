#include "server.h"
#include "backtrace.h"
#include <misc_log_ex.h>
#include <boost/program_options.hpp>
#include <boost/property_tree/ini_parser.hpp>
#include "requests.h"
#include "requestdefines.h"
#include "requests/sendsupernodeannouncerequest.h"
#include "rta/supernode.h"
#include "rta/fullsupernodelist.h"

namespace consts {
   static const char * DATA_PATH = "supernode/data";
   static const char * STAKE_WALLET_PATH = "stake-wallet";
   static const char * WATCHONLY_WALLET_PATH = "stake-wallet";
   static const size_t DEFAULT_STAKE_WALLET_REFRESH_INTERFAL_MS = 5 * 1000;
}

namespace tools {
    // TODO: make it crossplatform
    std::string getHomeDir()
    {
        return std::string(getenv("HOME"));
    }
}

namespace graft {

static std::function<void (int sig_num)> stop_handler;
static void signal_handler_stop(int sig_num)
{
    if(stop_handler) stop_handler(sig_num);
}

void GraftServer::setHttpRouters(HttpConnectionManager& httpcm)
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
}

void GraftServer::setCoapRouters(CoapConnectionManager& coapcm)
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

void GraftServer::initGlobalContext()
{
    graft::Context ctx(m_looper->getGcm());
    const ConfigOpts& copts = m_looper->getCopts();

    ctx.global["testnet"] = copts.testnet;
    ctx.global["watchonly_wallets_path"] = copts.watchonly_wallets_path;
    ctx.global["cryptonode_rpc_address"] = copts.cryptonode_rpc_address;
}

bool GraftServer::init(int argc, const char** argv)
{
    initSignals();

    bool res = initConfigOption(argc, argv);
    if(!res) return false;

    assert(!m_looper);
    m_looper = std::make_unique<Looper>(m_configOpts);
    assert(m_looper);

    intiConnectionManagers();

    prepareDataDirAndSupernodes();

    addGlobalCtxCleaner();

    startSupernodePeriodicTasks();


    for(auto& cm : m_conManagers)
    {
        cm->enableRouting();
        checkRoutes(*cm);
        cm->bind(*m_looper);
    }

    initGlobalContext();

    return true;
}

void GraftServer::serve()
{
    LOG_PRINT_L0("Starting server on: [http] " << m_configOpts.http_address << ", [coap] " << m_configOpts.coap_address);

    stop_handler = [this](int sig_num)
    {
        LOG_PRINT_L0("Stoping server");
        m_looper->stop();
    };

    m_looper->serve();

    stop_handler = nullptr;
}

void GraftServer::initSignals()
{
    struct sigaction sa;

    sigemptyset(&sa.sa_mask);

    sa.sa_sigaction = graft_bt_sighandler;
    sa.sa_flags = SA_SIGINFO;

    ::sigaction(SIGSEGV, &sa, NULL);

    sa.sa_sigaction = NULL;
    sa.sa_flags = 0;
    sa.sa_handler = signal_handler_stop;
    ::sigaction(SIGINT, &sa, NULL);
    ::sigaction(SIGTERM, &sa, NULL);
}

void GraftServer::initLog(int log_level)
{
    mlog_configure("", true);
    mlog_set_log_level(log_level);
}

bool GraftServer::initConfigOption(int argc, const char** argv)
{
    namespace po = boost::program_options;
    using namespace std;

    int log_level = 1;
    string config_filename;

    po::options_description desc("Allowed options");
    desc.add_options()
            ("help", "produce help message")
            ("config-file", po::value<string>(), "config filename (config.ini by default)")
            ("log-level", po::value<int>(), "log-level. (3 by default)");

    po::variables_map vm;
    po::store(po::parse_command_line(argc, argv, desc), vm);
    po::notify(vm);

    if (vm.count("help")) {
        cout << desc << "\n";
        return false;
    }

    if (vm.count("config-file")) {
        config_filename = vm["config-file"].as<string>();
    }
    if (vm.count("log-level")) {
        log_level = vm["log-level"].as<int>();
    }

    initLog(log_level);

    // load config
    boost::property_tree::ptree config;
    namespace fs = boost::filesystem;

    if (config_filename.empty()) {
        fs::path selfpath = argv[0];
        selfpath = selfpath.remove_filename();
        config_filename  = (selfpath /= "config.ini").string();
    }

    boost::property_tree::ini_parser::read_ini(config_filename, config);
    // now we have only following parameters
    // [server]
    //  address <IP>:<PORT>
    //  workers-count <integer>
    //  worker-queue-len <integer>
    //  stake-wallet <string> # stake wallet filename (no path)
    // [cryptonode]
    //  rpc-address <IP>:<PORT>
    //  p2p-address <IP>:<PORT> #maybe
    // [upstream]
    //  uri_name=uri_value #pairs for uri substitution
    //

    // data directory structure
    //        .
    //        ├── stake-wallet
    //        │   ├── stake-wallet
    //        │   ├── stake-wallet.address.txt
    //        │   └── stake-wallet.keys
    //        └── watch-only-wallets
    //            ├── supernode_tier1_1
    //            ├── supernode_tier1_1.address.txt
    //            ├── supernode_tier1_1.keys
    //            ................................
    //            ├── supernode_tier1_2
    //            ├── supernode_tier1_2.address.txt
    //            └── supernode_tier1_2.keys

    const boost::property_tree::ptree& server_conf = config.get_child("server");
    m_configOpts.http_address = server_conf.get<string>("http-address");
    m_configOpts.coap_address = server_conf.get<string>("coap-address");
    m_configOpts.timer_poll_interval_ms = server_conf.get<int>("timer-poll-interval-ms");
    m_configOpts.http_connection_timeout = server_conf.get<double>("http-connection-timeout");
    m_configOpts.workers_count = server_conf.get<int>("workers-count");
    m_configOpts.worker_queue_len = server_conf.get<int>("worker-queue-len");
    m_configOpts.upstream_request_timeout = server_conf.get<double>("upstream-request-timeout");
    m_configOpts.data_dir = server_conf.get<string>("data-dir", string());
    m_configOpts.lru_timeout_ms = server_conf.get<int>("lru-timeout-ms");
    m_configOpts.testnet = server_conf.get<bool>("testnet", false);
    m_configOpts.stake_wallet_name = server_conf.get<string>("stake-wallet-name", "stake-wallet");
    m_configOpts.stake_wallet_refresh_interval_ms = server_conf.get<size_t>("stake-wallet-refresh-interval-ms",
                                                                      consts::DEFAULT_STAKE_WALLET_REFRESH_INTERFAL_MS);
    if (m_configOpts.data_dir.empty()) {
        boost::filesystem::path p = boost::filesystem::absolute(tools::getHomeDir());
        p /= ".graft/";
        p /= consts::DATA_PATH;
        m_configOpts.data_dir = p.string();
    }

    const boost::property_tree::ptree& cryptonode_conf = config.get_child("cryptonode");
    m_configOpts.cryptonode_rpc_address = cryptonode_conf.get<string>("rpc-address");
    // m_configOpts.cryptonode_p2p_address = cryptonode_conf.get<string>("p2p-address");

    const boost::property_tree::ptree& uri_subst_conf = config.get_child("upstream");
    graft::OutHttp::uri_substitutions.clear();
    std::for_each(uri_subst_conf.begin(), uri_subst_conf.end(),[&uri_subst_conf](auto it)
    {
        std::string name(it.first);
        std::string val(uri_subst_conf.get<string>(name));
        graft::OutHttp::uri_substitutions.insert({std::move(name), std::move(val)});
    });

    return true;
}

void GraftServer::prepareDataDirAndSupernodes()
{
    // create data directory if not exists
    boost::filesystem::path data_path(m_configOpts.data_dir);
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

    std::cout << boost::filesystem::absolute(data_path).string() << std::endl;
    m_configOpts.watchonly_wallets_path = watchonly_wallets_path.string();

    // create supernode instance and put it into global context
    graft::SupernodePtr supernode = boost::make_shared<graft::Supernode>(
                    (stake_wallet_path / m_configOpts.stake_wallet_name).string(),
                    "", // TODO
                    m_configOpts.cryptonode_rpc_address,
                    m_configOpts.testnet
                    );

    supernode->setNetworkAddress(m_configOpts.http_address + "/dapi/v2.0");

    // create fullsupernode list instance and put it into global context
    LOG_PRINT_L0("loading supernode list");
    graft::FullSupernodeListPtr fsl = boost::make_shared<graft::FullSupernodeList>(
                m_configOpts.cryptonode_rpc_address, m_configOpts.testnet);
    size_t found_wallets = 0;
    size_t loaded_wallets = fsl->loadFromDirThreaded(watchonly_wallets_path.string(), found_wallets);

    if (found_wallets != loaded_wallets) {
        LOG_ERROR("found wallets: " << found_wallets << ", loaded wallets: " << loaded_wallets);
    }
    LOG_PRINT_L0("supernode list loaded");

    // add our supernode as well, it wont be added from announce;
    fsl->add(supernode);

    //put fsl into global context
    assert(m_looper);
    graft::Context ctx(m_looper->getGcm());
    ctx.global["supernode"] = supernode;
    ctx.global[CONTEXT_KEY_FULLSUPERNODELIST] = fsl;
}

void GraftServer::intiConnectionManagers()
{
    auto httpcm = std::make_unique<graft::HttpConnectionManager>();
    setHttpRouters(*httpcm);
    m_conManagers.emplace_back(std::move(httpcm));

    auto coapcm = std::make_unique<graft::CoapConnectionManager>();
    setCoapRouters(*coapcm);
    m_conManagers.emplace_back(std::move(coapcm));
}

void GraftServer::addGlobalCtxCleaner()
{
    auto cleaner = [](const graft::Router::vars_t& vars, const graft::Input& input, graft::Context& ctx, graft::Output& output)->graft::Status
    {
        graft::Context::GlobalFriend::cleanup(ctx.global);
        return graft::Status::Ok;
    };
    m_looper->addPeriodicTask(
                graft::Router::Handler3(nullptr, cleaner, nullptr),
                std::chrono::milliseconds(m_configOpts.lru_timeout_ms)
                );
}

void GraftServer::startSupernodePeriodicTasks()
{
    // update supernode every interval_ms
    auto supernodeRefreshWorker = [](const graft::Router::vars_t& vars, const graft::Input& input, graft::Context& ctx,
            graft::Output& output)->graft::Status
    {


        switch (ctx.local.getLastStatus()) {
        case graft::Status::Forward: // reply from cryptonode
            return graft::Status::Ok;
        case graft::Status::Ok:
        case graft::Status::None:
            graft::SupernodePtr supernode;

            LOG_PRINT_L1("supernodeRefreshWorker");
            LOG_PRINT_L1("input: " << input.data());
            LOG_PRINT_L1("output: " << output.data());
            LOG_PRINT_L1("last status: " << (int)ctx.local.getLastStatus());

            supernode = ctx.global.get(CONTEXT_KEY_SUPERNODE, graft::SupernodePtr(nullptr));


            if (!supernode.get()) {
                LOG_ERROR("supernode is not set in global context");
                return graft::Status::Error;
            }

            LOG_PRINT_L0("about to refresh supernode: " << supernode->walletAddress());

            if (!supernode->refresh()) {
                return graft::Status::Ok;
            }

            LOG_PRINT_L0("supernode refresh done, stake amount: " << supernode->stakeAmount());

            graft::SendSupernodeAnnounceJsonRpcRequest req;
            if (!supernode->prepareAnnounce(req.params)) {
                LOG_ERROR("Can't prepare announce");
                return graft::Status::Ok;
            }

            req.method = "send_supernode_announce";
            req.id = 0;
            output.load(req);

            output.path = "/json_rpc/rta";
            // DBG: without cryptonode
            // output.path = "/dapi/v2.0/send_supernode_announce";

            LOG_PRINT_L1("Calling cryptonode: sending announce");
            return graft::Status::Forward;
        }
    };
    size_t initial_interval_ms = 3000;
    assert(m_looper);
    m_looper->addPeriodicTask(
                graft::Router::Handler3(nullptr, supernodeRefreshWorker, nullptr),
                std::chrono::milliseconds(m_configOpts.stake_wallet_refresh_interval_ms),
                std::chrono::milliseconds(initial_interval_ms)
                );
}

void GraftServer::checkRoutes(graft::ConnectionManager& cm)
{//check conflicts in routes
    std::string s = cm.dbgCheckConflictRoutes();
    if(!s.empty())
    {
        std::cout << std::endl << "==> " << cm.getName() << " manager.dbgDumpRouters()" << std::endl;
        std::cout << cm.dbgDumpRouters();

        //if you really need dump of r3tree uncomment two following lines
        //std::cout << std::endl << std::endl << "==> manager.dbgDumpR3Tree()" << std::endl;
        //manager.dbgDumpR3Tree();

        throw std::runtime_error("Routes conflict found:" + s);
    }
}

}//namespace graft
