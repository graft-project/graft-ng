#include "server.h"
#include "backtrace.h"
#include "log.h"
#include <boost/program_options.hpp>
#include <boost/property_tree/ini_parser.hpp>
#include "requests.h"

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
    Router::Handler3 h3_test1(http_test, nullptr, nullptr);

    dapi_router.addRoute("/test", METHOD_GET, h3_test1);
    httpcm.addRouter(dapi_router);

    Router http_router;
    graft::registerRTARequests(http_router);
    httpcm.addRouter(http_router);

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

bool GraftServer::init(int argc, const char** argv)
{
    initSignals();

    bool res = initConfigOption(argc, argv);
    if(!res) return false;

    assert(!m_looper);
    m_looper = std::make_unique<Looper>(m_configOpts);
    assert(m_looper);

    addGlobalCtxCleaner();

    intiConnectionManagers();


    for(auto& cm : m_conManagers)
    {
        cm->enableRouting();
        checkRoutes(*cm);
        cm->bind(*m_looper);
    }
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

namespace details
{

namespace po = boost::program_options;
using namespace std;

void init_log(const boost::property_tree::ptree& config, const po::variables_map& vm)
{
    int log_level = 3;
    bool log_console = true;
    std::string log_filename;

    //from config
    const boost::property_tree::ptree& log_conf = config.get_child("logging");
    boost::optional<int> level  = log_conf.get_optional<int>("loglevel");
    if(level) log_level = level.get();
    boost::optional<std::string> log_file  = log_conf.get_optional<string>("logfile");
    if(log_file) log_filename = log_file.get();
    boost::optional<bool> log_to_console  = log_conf.get_optional<bool>("console");
    if(log_to_console) log_console = log_to_console.get();

    //override from cmdline
    if (vm.count("log-level")) log_level = vm["log-level"].as<int>();
    if (vm.count("log-file")) log_filename = vm["log-file"].as<string>();
    if (vm.count("log-console")) log_console = vm["log-console"].as<bool>();

    mlog_configure(log_filename, log_console);
    mlog_set_log_level(log_level);
}

} //namespace details

bool GraftServer::initConfigOption(int argc, const char** argv)
{
    namespace po = boost::program_options;
    using namespace std;

    string config_filename;
    po::variables_map vm;

    {
        po::options_description desc("Allowed options");
        desc.add_options()
                ("help", "produce help message")
                ("config-file", po::value<string>(), "config filename (config.ini by default)")
                ("log-level", po::value<int>(), "log-level. (3 by default)")
                ("log-console", po::value<bool>(), "log to console. 1 or true or 0 or false. (true by default)")
                ("log-file", po::value<string>(), "log file");

        po::store(po::parse_command_line(argc, argv, desc), vm);
        po::notify(vm);

        if (vm.count("help")) {
            cout << desc << "\n";
            return false;
        }

        if (vm.count("config-file")) {
            config_filename = vm["config-file"].as<string>();
        }
    }

    // load config
    boost::property_tree::ptree config;
    namespace fs = boost::filesystem;

    if (config_filename.empty()) {
        fs::path selfpath = argv[0];
        selfpath = selfpath.remove_filename();
        config_filename  = (selfpath / "config.ini").string();
    }

    boost::property_tree::ini_parser::read_ini(config_filename, config);
    // now we have only following parameters
    // [server]
    //  address <IP>:<PORT>
    //  workers-count <integer>
    //  worker-queue-len <integer>
    // [cryptonode]
    //  rpc-address <IP>:<PORT>
    //  p2p-address <IP>:<PORT> #maybe
    // [upstream]
    //  uri_name=uri_value #pairs for uri substitution
    //

    details::init_log(config, vm);

    const boost::property_tree::ptree& server_conf = config.get_child("server");
    m_configOpts.http_address = server_conf.get<string>("http-address");
    m_configOpts.coap_address = server_conf.get<string>("coap-address");
    m_configOpts.timer_poll_interval_ms = server_conf.get<int>("timer-poll-interval-ms");
    m_configOpts.http_connection_timeout = server_conf.get<double>("http-connection-timeout");
    m_configOpts.workers_count = server_conf.get<int>("workers-count");
    m_configOpts.worker_queue_len = server_conf.get<int>("worker-queue-len");
    m_configOpts.upstream_request_timeout = server_conf.get<double>("upstream-request-timeout");
    m_configOpts.data_dir = server_conf.get<string>("data-dir");
    m_configOpts.lru_timeout_ms = server_conf.get<int>("lru-timeout-ms");

    const boost::property_tree::ptree& cryptonode_conf = config.get_child("cryptonode");
    m_configOpts.cryptonode_rpc_address = cryptonode_conf.get<string>("rpc-address");
    //m_configOpts.cryptonode_p2p_address = cryptonode_conf.get<string>("p2p-address");

    const boost::property_tree::ptree& log_conf = config.get_child("logging");

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
