
#include "supernode/server/server.h"
#include "supernode/server/config.h"

#include "backtrace.h"
#include "GraftletLoader.h"
#include "sys_info.h"

#undef MONERO_DEFAULT_LOG_CATEGORY
#define MONERO_DEFAULT_LOG_CATEGORY "supernode.server"

namespace graft::supernode::server {

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

Server::Server()
{
}

Server::~Server()
{
}

bool Server::init(Config& cfg)
{
    create_looper(cfg);
    create_system_info_counter();
    initGraftlets();
    add_global_ctx_cleaner();

    initGlobalContext();

    initMisc(cfg);

    initConnectionManagers();
    initRouters();
    initGraftletRouters();

    for(auto& it : m_conManagers)
    {
        ConnectionManager& cm = *it.second.get();
        cm.enableRouting();
        utils::check_routes(cm);
        cm.bind(*m_looper);
    }

    return true;
}

RunResult Server::run(void)
{
    initSignals();

    auto res = RunResult::UnexpectedOk;

    //shutdown
    int_handler = [this, &res](int sig_num)
    {
        LOG_PRINT_L0("Stopping server");
        stop();
        res = RunResult::SignalShutdown;
    };

    //terminate
    term_handler = [this, &res](int sig_num)
    {
        LOG_PRINT_L0("Force stopping server");
        stop(true);
        res = RunResult::SignalTerminate;
    };

    //restart
    hup_handler = [this, &res](int sig_num)
    {
        LOG_PRINT_L0("Restarting server");
        stop();
        res = RunResult::SignalRestart;
    };

    serve();

    return res;
}

void Server::initSignals()
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

Config& Server::config()
{
    assert(m_looper);
    return m_looper->config();
}

bool Server::ready(void) const
{
    return m_looper && m_looper->ready();
}

void Server::stop(bool force)
{
    m_looper->stop(force);
}

void Server::create_looper(Config& cfg)
{
    assert(!m_looper);
    m_looper = std::make_unique<Looper>(cfg);
    assert(m_looper);
}

void Server::create_system_info_counter(void)
{
    assert(!m_sys_info);
    m_sys_info = std::make_unique<SysInfoCounter>();
    assert(m_sys_info);
}

void Server::initGraftlets()
{
    if(m_graftletLoader) return;

    m_graftletLoader = std::make_unique<graftlet::GraftletLoader>();
    LOG_PRINT_L1("Searching graftlets");
    for(auto& it : config().graftlet_dirs)
    {
        LOG_PRINT_L1("Searching graftlets in directory '") << it << "'";
        m_graftletLoader->findGraftletsInDirectory(it, "so");
    }
    m_graftletLoader->checkDependencies();
}

void Server::initGraftletRouters()
{
    ConnectionManager* cm = getConMgr("HTTP");
    assert(cm);
    assert(m_graftletLoader);
    IGraftlet::EndpointsVec endpoints = m_graftletLoader->getEndpoints();
    if(!endpoints.empty())
    {
        Router graftlet_router;
        for(const auto& item : endpoints)
        {
            const std::string& endpoint = std::get<0>(item);
            const int& method = std::get<1>(item);
            const Router::Handler& handler = std::get<2>(item);

            graftlet_router.addRoute(endpoint, method, {nullptr, handler , nullptr});
        }
        cm->addRouter(graftlet_router);
    }
}

void Server::initGlobalContext()
{
//  TODO: why context intialized second time here?
    //ANSWER: It is correct. The ctx is not initialized, ctx is attached to
    //  the global part of the context to which we want to get access here, only
    //  the local part of it has lifetime the same as the lifetime of ctx variable.
    Context ctx(m_looper->gcm());
//    ctx.global["testnet"] = copts.testnet;
//    ctx.global["watchonly_wallets_path"] = copts.watchonly_wallets_path;
//    ctx.global["cryptonode_rpc_address"] = copts.cryptonode_rpc_address;
    assert(m_sys_info);
    ctx.runtime_sys_info(*(m_sys_info.get()));
    ctx.config(m_looper->config());
}

void Server::serve()
{
    LOG_PRINT_L0("Starting server on: [http] " << config().http_address << ", [coap] " << config().coap_address);
    m_looper->serve();
}

namespace details
{

void initGraftletDirs(const Config& cfg, const std::string& dirs_opt, bool dirs_opt_exists, std::vector<std::string>& graftlet_dirs)
{//configOpts.graftlet_dirs
    namespace fs = boost::filesystem;

    graftlet_dirs.clear();

    fs::path self_dir = cfg.self_dir;
    self_dir = self_dir.remove_filename();

    if(!dirs_opt_exists)
    {
        graftlet_dirs.push_back(fs::complete("graftlets", self_dir).string());
        return;
    }

    fs::path cur_dir(fs::current_path());

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
                graftlet_dirs.emplace_back(it.string());
            else
                LOG_PRINT_L1("Graftlet path '" << it.string() << "' is not a directory");
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

} //namespace details


ConnectionManager* Server::getConMgr(const ConnectionManager::Proto& proto)
{
    auto it = m_conManagers.find(proto);
    assert(it != m_conManagers.end());
    return it->second.get();
}

void Server::initConnectionManagers()
{
    std::unique_ptr<HttpConnectionManager> httpcm = std::make_unique<HttpConnectionManager>();
    auto res1 = m_conManagers.emplace(httpcm->getProto(), std::move(httpcm));
    assert(res1.second);
    std::unique_ptr<CoapConnectionManager> coapcm = std::make_unique<CoapConnectionManager>();
    auto res2 = m_conManagers.emplace(coapcm->getProto(), std::move(coapcm));
    assert(res2.second);
}


void Server::add_global_ctx_cleaner()
{
    auto cleaner = [](const graft::Router::vars_t& vars, const graft::Input& input, graft::Context& ctx, graft::Output& output)->graft::Status
    {
        graft::Context::GlobalFriend::cleanup(ctx.global);
        return graft::Status::Ok;
    };
    m_looper->addPeriodicTask(
                graft::Router::Handler3(nullptr, cleaner, nullptr),
                std::chrono::milliseconds(m_looper->config().lru_timeout_ms)
                );
}

}

