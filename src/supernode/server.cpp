
#include "supernode/server.h"
#include "lib/graft/backtrace.h"
#include "lib/graft/GraftletLoader.h"
#include "lib/graft/sys_info.h"
#include "lib/graft/graft_exception.h"
#include "lib/graft/ConfigIni.h"
#include "version.h"

#include <boost/program_options.hpp>
#include <regex>

#undef MONERO_DEFAULT_LOG_CATEGORY
#define MONERO_DEFAULT_LOG_CATEGORY "supernode.server"

namespace consts {
   static const char * DATA_PATH = "supernode/data";
}

namespace tools {
    // TODO: make it crossplatform
    std::string getHomeDir()
    {
        return std::string(getenv("HOME"));
    }
}

namespace graft {

static std::function<void (int sig_num)> int_handler, term_handler, hup_handler;

static void signal_handler_shutdown(int sig_num)
{
    if(int_handler) int_handler(sig_num);
}

static void signal_handler_terminate(int sig_num)
{
    if(term_handler) term_handler(sig_num);
}

static void signal_handler_restart(int sig_num)
{
    if(hup_handler) hup_handler(sig_num);
}

GraftServer::GraftServer()
{
    m_connectionBase = std::make_unique<ConnectionBase>();
    m_connectionBaseReady = true;
}

GraftServer::~GraftServer()
{
}

ConfigOpts& GraftServer::getCopts()
{
    assert(m_connectionBase);
    return m_connectionBase->getCopts();
}

void GraftServer::setSysInfoCounter(std::unique_ptr<SysInfoCounter> counter)
{
    //Call the function with derived SysInfoCounter once only if required.
    //Otherwise, default SysInfoCounter will be created.
    //This requirement exists because it is possible that the counter to be replaced already has counted something.
    assert(m_connectionBase);
    m_connectionBase->setSysInfoCounter(counter);
}

void GraftServer::getThreadPoolInfo(uint64_t& activeWorkers, uint64_t& expelledWorkers) const
{
    assert(m_connectionBase);
    m_connectionBase->getLooper().getThreadPoolInfo(activeWorkers, expelledWorkers);
}

void GraftServer::initGraftlets()
{
    if(m_graftletLoader) return;
    m_graftletLoader = std::make_unique<graftlet::GraftletLoader>(getCopts().common, getLooper().getGcm());
    LOG_PRINT_L1("Searching graftlets");
    for(auto& it : getCopts().graftlet_dirs)
    {
        LOG_PRINT_L1("Searching graftlets in directory '") << it << "'";
        m_graftletLoader->findGraftletsInDirectory(it, "so");
    }
    m_graftletLoader->checkDependencies();
}

void GraftServer::initGraftletRouters()
{
    ConnectionManager* cm = getConMgr("HTTP");
    assert(cm);
    assert(m_graftletLoader);
    IGraftlet::EndpointsVec endpoints = m_graftletLoader->getEndpoints();
    if(!endpoints.empty())
    {
        Router graftlet_router;
        for(auto& item : endpoints)
        {
            std::string& endpoint = std::get<0>(item);
            int& method = std::get<1>(item);
            Router::Handler& handler = std::get<2>(item);

            graftlet_router.addRoute(endpoint, method, {nullptr, handler , nullptr});
        }
        cm->addRouter(graftlet_router);
    }
}

void GraftServer::initGraftletPeriodics()
{
    assert(m_graftletLoader);
    IGraftlet::PeriodicVec periodics = m_graftletLoader->getPeriodics();
    for(IGraftlet::Periodic& p : periodics)
    {
        getLooper().addPeriodicTask(p.handler, std::chrono::milliseconds( p.interval_ms ),
                                    std::chrono::milliseconds( p.initial_interval_ms ),
                                    p.random_factor);
    }
}


void GraftServer::initGlobalContext()
{
//  TODO: why context intialized second time here?
    //ANSWER: It is correct. The ctx is not initialized, ctx is attached to
    //  the global part of the context to which we want to get access here, only
    //  the local part of it has lifetime the same as the lifetime of ctx variable.
    assert(m_connectionBase);
    Context ctx(m_connectionBase->getLooper().getGcm());
    const ConfigOpts& copts = getCopts();
//  copts is empty here

//    ctx.global["testnet"] = copts.testnet;
//    ctx.global["watchonly_wallets_path"] = copts.watchonly_wallets_path;
//    ctx.global["cryptonode_rpc_address"] = copts.cryptonode_rpc_address;
    ctx.global["graftletLoader"] = m_graftletLoader.get();
}

void GraftServer::initMisc(ConfigOpts& configOpts)
{

}

void GraftServer::addGenericCallbackRoute()
{
    auto genericCallback = [](const Router::vars_t& vars, const Input&, Context& ctx, Output& output)->Status
    {
        if (vars.count("id") == 0)
        {
            output.body = "Cannot find callback UUID";
            return Status::Error;
        }
        std::string id = vars.find("id")->second;
        boost::uuids::string_generator sg;
        boost::uuids::uuid uuid = sg(id);
        ctx.setNextTaskId(uuid);
        return Status::Ok;
    };
    Router router;
    router.addRoute("/callback/{id:[0-9a-fA-F-]+}",METHOD_POST,{nullptr,genericCallback,nullptr});
    ConnectionManager* httpcm = getConMgr("HTTP");
    httpcm->addRouter(router);
}

bool GraftServer::init(int argc, const char** argv, ConfigOpts& configOpts)
{
    assert(m_connectionBase);
    m_connectionBase->createSystemInfoCounter();

    bool res = initConfigOption(argc, argv, configOpts);
    if(!res) return false;

    m_connectionBase->loadBlacklist(configOpts);
    m_connectionBase->createLooper(configOpts);
    initGraftlets();
    addGlobalCtxCleaner();

    initGlobalContext();

    initMisc(configOpts);

    m_connectionBase->initConnectionManagers();
    addGenericCallbackRoute();
    initRouters();
    initGraftletRouters();

    m_connectionBase->bindConnectionManagers();

    initGraftletPeriodics();

    return true;
}

void GraftServer::serve()
{
    LOG_PRINT_L0("Starting server on: [http] " << getCopts().http_address << ", [coap] " << getCopts().coap_address
                 << ", version: " << GRAFT_SUPERNODE_VERSION_FULL);

    m_connectionBase->getLooper().serve();
}

GraftServer::RunRes GraftServer::run()
{
    initSignals();

    RunRes res = RunRes::UnexpectedOk;

    //shutdown
    int_handler = [this, &res](int sig_num)
    {
        LOG_PRINT_L0("Stopping server");
        stop();
        res = RunRes::SignalShutdown;
    };

    //terminate
    term_handler = [this, &res](int sig_num)
    {
        LOG_PRINT_L0("Force stopping server");
        stop(true);
        res = RunRes::SignalTerminate;
    };

    //restart
    hup_handler = [this, &res](int sig_num)
    {
        LOG_PRINT_L0("Restarting server");
        stop();
        res = RunRes::SignalRestart;
    };

    serve();

    return res;
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
    sa.sa_handler = signal_handler_shutdown;
    ::sigaction(SIGINT, &sa, NULL);

    sa.sa_handler = signal_handler_terminate;
    ::sigaction(SIGTERM, &sa, NULL);

    sa.sa_handler = signal_handler_restart;
    ::sigaction(SIGHUP, &sa, NULL);
}

namespace details
{

std::string trim_comments(std::string s)
{
    //remove ;; tail
    std::size_t pos = s.find(";;");
    if(pos != std::string::npos)
    {
      s = s.substr(0,pos);
    }
    boost::trim_right(s);
    return s;
}

namespace po = boost::program_options;

void init_log(const ConfigIniSubtree& config, const po::variables_map& vm)
{
    std::string log_level = "3";
    bool log_console = true;
    std::string log_filename;
    std::string log_format;

    //from config
    auto log_conf = config.get_child("logging");
    std::optional<std::string> level  = log_conf.get_optional<std::string>("loglevel");
    if(level) log_level = trim_comments( level.value() );
    std::optional<std::string> log_file  = log_conf.get_optional<std::string>("logfile");
    if(log_file) log_filename = trim_comments( log_file.value() );
    std::optional<bool> log_to_console  = log_conf.get_optional<bool>("console");
    if(log_to_console) log_console = log_to_console.value();
    std::optional<std::string> log_fmt  = log_conf.get_optional<std::string>("log-format");
    if(log_fmt) log_format = trim_comments( log_fmt.value() );

    //override from cmdline
    if (vm.count("log-level")) log_level = vm["log-level"].as<std::string>();
    if (vm.count("log-file")) log_filename = vm["log-file"].as<std::string>();
    if (vm.count("log-console")) log_console = vm["log-console"].as<bool>();
    if (vm.count("log-format")) log_format = vm["log-format"].as<std::string>();

    // default log format (we need to explicitly apply it here, otherwise full path to a file will be logged  with monero default format)
    static const char * DEFAULT_LOG_FORMAT = "%datetime{%Y-%M-%d %H:%m:%s.%g}\t%thread\t%level\t%logger\t%rfile:%line\t%msg";
    if(log_format.empty()) log_format = DEFAULT_LOG_FORMAT;

#ifdef ELPP_SYSLOG
        if(log_filename == "syslog")
        {
            INITIALIZE_SYSLOG("graft_server");
            mlog_syslog = true;
            mlog_configure("", false, log_format.empty()? nullptr : log_format.c_str());
        }
        else
#endif
        {
            mlog_configure(log_filename, log_console, log_format.empty()? nullptr : log_format.c_str());
        }

        mlog_set_log(log_level.c_str());
}

void initGraftletDirs(int argc, const char** argv, const std::string& dirs_opt, bool dirs_opt_exists, std::vector<std::string>& graftlet_dirs)
{//configOpts.graftlet_dirs
    namespace fs = boost::filesystem;

    graftlet_dirs.clear();

    fs::path self_dir = argv[0];
    self_dir = self_dir.remove_filename();

    if(!dirs_opt_exists)
    {
        graftlet_dirs.push_back(fs::complete("graftlets", self_dir).string());
        return;
    }

    fs::path cur_dir( fs::current_path() );

    //if list empty then load none graftlet
    if(dirs_opt.empty()) return;

    //split and fill set
    std::set<fs::path> set;
    for(std::string::size_type s = 0;;)
    {
        std::string::size_type e = dirs_opt.find(':',s);
        if(e == std::string::npos)
        {
            set.insert(dirs_opt.substr(s));
            break;
        }
        set.insert(dirs_opt.substr(s,e-s));
        s = e + 1;
    }
    for(auto& it : set)
    {
        if(it.is_relative())
        {
            bool found = false;
            fs::path path1 = fs::complete(it, self_dir);
            if(fs::is_directory(path1))
            {
                graftlet_dirs.push_back(path1.string());
                found = true;
            }
            if(self_dir != cur_dir)
            {
                fs::path path2 = fs::complete(it, cur_dir);
                if(fs::is_directory(path2))
                {
                    graftlet_dirs.push_back(path2.string());
                    found = true;
                }
            }
            if(!found)
            {
                LOG_PRINT_L1("Graftlet path '" << it.string() << "' is not a directory");
            }
        }
        else
        {
            if(fs::is_directory(it))
            {
                graftlet_dirs.push_back(it.string());
            }
            else
            {
                LOG_PRINT_L1("Graftlet path '" << it.string() << "' is not a directory");
            }
        }
    }
    {//remove duplicated dirs
        std::set<fs::path> set;
        auto end = std::remove_if(graftlet_dirs.begin(), graftlet_dirs.end(),
                                  [&set](auto& s)->bool{ return !set.emplace(s).second; }
        );
        graftlet_dirs.erase(end, graftlet_dirs.end());
    }
}

void parseSubstitutionItem(const std::string& name, const std::string& val, std::string& uri, int& cnt, bool& keepAlive, double& timeout)
{
    std::string s = trim_comments(val);
    std::regex regex(R"(^\s*([^,\s]+)\s*(,\s*(\d+)\s*(,\s*(true|false|0|1)\s*(,\s*(\d+\.?\d*)\s*)?)?)?\s*$)");
    std::smatch m;
    if(!std::regex_match(s, m, regex))
    {
        std::ostringstream oss;
        oss << "invalid [upstream] format line with name '" << name << "' : '" << val << "'";
        throw graft::exit_error(oss.str());
    }
    assert(7 < m.size());
    assert(m[1].matched);
    uri = m[1];
    cnt = 0; keepAlive = false; timeout = 0;
    if(!m[3].matched) return;
    cnt = std::stoi(m[3]);
    if(!m[5].matched) return;
    if(m[5] == "true" || m[5] == "1") keepAlive = true;
    if(!m[7].matched) return;
    timeout = std::stod(m[7]);
}

} //namespace details

void usage(const boost::program_options::options_description& desc)
{
    std::string sigmsg = "Supported signals:\n"
            "  INT  - Shutdown server gracefully closing all pending tasks.\n"
            "  TEMP - Shutdown server even if there are pending tasks.\n"
            "  HUP  - Restart server with updated configuration parameters.\n";

    std::cout << desc << "\n" << sigmsg << "\n";
}

void print_version()
{
    std::cout << "Graft supernode version: " << GRAFT_SUPERNODE_VERSION_FULL << "\n";
}

bool GraftServer::initConfigOption(int argc, const char** argv, ConfigOpts& configOpts)
{
    namespace po = boost::program_options;

    std::string config_filename;
    po::variables_map vm;

    {
        po::options_description desc("Allowed options");
        desc.add_options()
                ("help", "produce help message")
                ("version", "print version and exit")
                ("config-file", po::value<std::string>(), "config filename (config.ini by default)")
                ("log-level", po::value<std::string>(), "log-level. (3 by default), e.g. --log-level=2,supernode.task:INFO,supernode.server:DEBUG")
                ("log-console", po::value<bool>(), "log to console. 1 or true or 0 or false. (true by default)")
#ifdef ELPP_SYSLOG
                ("log-file", po::value<std::string>(), "log file; set it to syslog if you want use the syslog instead")
#else
                ("log-file", po::value<std::string>(), "log file")
#endif
                ("log-format", po::value<std::string>(), "e.g. %datetime{%Y-%M-%d %H:%m:%s.%g} %level %logger %rfile %msg");

        po::store(po::parse_command_line(argc, argv, desc), vm);
        po::notify(vm);

        if (vm.count("help")) {
            usage(desc);
            exit(EXIT_SUCCESS);
        }

        if (vm.count("version")) {
            print_version();
            exit(EXIT_SUCCESS);
        }

        if (vm.count("config-file")) {
            config_filename = vm["config-file"].as<std::string>();
        }
    }

    // load config
    namespace fs = boost::filesystem;

    if (config_filename.empty()) {
        fs::path selfpath = argv[0];
        selfpath = selfpath.remove_filename();
        config_filename  = (selfpath / "config.ini").string();
    }

    ConfigIniSubtree config = ConfigIniSubtree::create(config_filename);
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
    // [graftlets]
    // dirs <string:string:...>

    details::init_log(config, vm);
    //

    configOpts.common.config_filename = config_filename;

    ConfigIniSubtree server_conf = config.get_child("server");
    configOpts.http_address = server_conf.get<std::string>("http-address");
    configOpts.coap_address = server_conf.get<std::string>("coap-address");
    configOpts.timer_poll_interval_ms = server_conf.get<int>("timer-poll-interval-ms");
    configOpts.http_connection_timeout = server_conf.get<double>("http-connection-timeout");
    configOpts.workers_count = server_conf.get<int>("workers-count");
    configOpts.worker_queue_len = server_conf.get<int>("worker-queue-len");
    configOpts.workers_expelling_interval_ms = server_conf.get<int>("workers-expelling-interval-ms", 1000);
    configOpts.upstream_request_timeout = server_conf.get<double>("upstream-request-timeout");
    configOpts.lru_timeout_ms = server_conf.get<int>("lru-timeout-ms");
    configOpts.common.data_dir = server_conf.get<std::string>("data-dir");
    configOpts.common.wallet_public_address = server_conf.get<std::string>("wallet-public-address", "");
    configOpts.common.testnet = server_conf.get<bool>("testnet", false);

    //ipfilter
    auto opt_ipfilter = config.get_child_optional("ipfilter");
    if(opt_ipfilter)
    {
        IPFilterOpts& ipfilter = configOpts.ipfilter;
        const auto ipfilter_conf = opt_ipfilter.value();
        ipfilter.window_size_sec = ipfilter_conf.get<int>("window-size-sec", 0);
        ipfilter.requests_per_sec = ipfilter_conf.get<int>("requests-per-sec", 0);
        ipfilter.ban_ip_sec = ipfilter_conf.get<int>("ban-ip-sec", 0);
        //ipfilter.rules_filename
        ipfilter.rules_filename = ipfilter_conf.get<std::string>("rules", "");
        if(!ipfilter.rules_filename.empty())
        {
            ipfilter.rules_filename = details::trim_comments(ipfilter.rules_filename);
            fs::path path = ipfilter.rules_filename;
            if(path.is_relative())
            {
                fs::path selfpath = argv[0];
                selfpath.remove_filename();
                path = fs::complete(path, selfpath);
                ipfilter.rules_filename = path.string();
            }
        }
    }

    //configOpts.graftlet_dirs
    ConfigIniSubtree graftlets_conf = config.get_child("graftlets");
    std::optional<std::string> dirs_opt  = graftlets_conf.get_optional<std::string>("dirs");
    details::initGraftletDirs(argc, argv, (dirs_opt)? dirs_opt.value() : "", bool(dirs_opt), configOpts.graftlet_dirs);

    ConfigIniSubtree cryptonode_conf = config.get_child("cryptonode");
    configOpts.cryptonode_rpc_address = cryptonode_conf.get<std::string>("rpc-address");

    ConfigIniSubtree log_conf = config.get_child("logging");
    std::optional<int> log_trunc_to_size  = log_conf.get_optional<int>("trunc-to-size");
    configOpts.log_trunc_to_size = (log_trunc_to_size)? log_trunc_to_size.value() : -1;

    ConfigIniSubtree uri_subst_conf = config.get_child("upstream");
    graft::OutHttp::uri_substitutions.clear();
    std::for_each(uri_subst_conf.begin(), uri_subst_conf.end(),[&uri_subst_conf](auto it)
    {
        std::string name(it.name());
        std::string val(uri_subst_conf.get<std::string>(name));

        std::string uri; int cnt; bool keepAlive; double timeout;
        details::parseSubstitutionItem(name, val, uri, cnt, keepAlive, timeout);
        graft::OutHttp::uri_substitutions.emplace(std::move(name), std::make_tuple(std::move(uri), cnt, keepAlive, timeout));
    });

    prepareDataDir(configOpts);

    return true;
}

void GraftServer::prepareDataDir(ConfigOpts& configOpts)
{
    if (configOpts.common.data_dir.empty()) {
        boost::filesystem::path p = boost::filesystem::absolute(tools::getHomeDir());
        p /= ".graft/";
        p /= consts::DATA_PATH;
        configOpts.common.data_dir = p.string();
    }
}

void GraftServer::initRouters()
{

}

void GraftServer::addGlobalCtxCleaner()
{
    auto cleaner = [](const graft::Router::vars_t& vars, const graft::Input& input, graft::Context& ctx, graft::Output& output)->graft::Status
    {
        graft::Context::GlobalFriend::cleanup(ctx.global);
        return graft::Status::Ok;
    };
    m_connectionBase->getLooper().addPeriodicTask(
                graft::Router::Handler3(nullptr, cleaner, nullptr),
                std::chrono::milliseconds(m_connectionBase->getCopts().lru_timeout_ms)
                );
}

}//namespace graft
