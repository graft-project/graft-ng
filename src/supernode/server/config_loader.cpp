
#include "supernode/server/config_loader.h"
#include "supernode/server/config.h"
#include "common/utils.h"
#include <misc_log_ex.h>

#include <iostream>
#include <boost/program_options.hpp>
#include <boost/property_tree/ini_parser.hpp>
#include <boost/filesystem.hpp>

namespace graft::supernode::server {

namespace bpo = boost::program_options;
namespace bpt = boost::property_tree;

using VarMap = bpo::variables_map;
using OptDesc = bpo::options_description;
using PTree = bpt::ptree;

namespace fs = boost::filesystem;
using Path = fs::path;

void read_graftlet_dirs(Config& cfg, const std::string& dir_list);

void usage(const OptDesc& desc)
{
    std::string sigmsg = "Supported signals:\n"
        "  INT  - Shutdown server gracefully closing all pending tasks.\n"
        "  TEMP - Shutdown server even if there are pending tasks.\n"
        "  HUP  - Restart server with updated configuration parameters.\n";

    std::cout << desc << "\n" << sigmsg << "\n";
}

void read_log_params_from_ini(const PTree& ini_cfg, Config& cfg)
{
    const PTree& ini = ini_cfg.get_child("logging");

    boost::optional<std::string> level  = ini.get_optional<std::string>("loglevel");
    if(level)
        cfg.log_level = utils::trim_comments(level.get());

    boost::optional<std::string> log_file  = ini.get_optional<std::string>("logfile");
    if(log_file)
        cfg.log_filename = utils::trim_comments(log_file.get());

    boost::optional<bool> log_to_console  = ini.get_optional<bool>("console");
    if(log_to_console)
        cfg.log_console = log_to_console.get();

    boost::optional<std::string> log_fmt  = ini.get_optional<std::string>("log-format");
    if(log_fmt)
        cfg.log_format = utils::trim_comments(log_fmt.get());
}

void read_log_params_from_cmd_line(const VarMap& cl_cfg, Config& cfg)
{
    if(cl_cfg.count("log-level"))     cfg.log_level     = cl_cfg["log-level"].as<std::string>();
    if(cl_cfg.count("log-file"))      cfg.log_filename  = cl_cfg["log-file"].as<std::string>();
    if(cl_cfg.count("log-console"))   cfg.log_console   = cl_cfg["log-console"].as<bool>();
    if(cl_cfg.count("log-format"))    cfg.log_format    = cl_cfg["log-format"].as<std::string>();
}

bool read_params_from_cmd_line(const int argc, const char** argv, Config& cfg, VarMap& vm)
{
    bool exit = false;

    OptDesc desc("Allowed options");
    desc.add_options()
        ("help", "produce help message")
        ("config-file",   bpo::value<std::string>(), "config filename (config.ini by default)")
        ("log-level",     bpo::value<std::string>(), "log-level. (3 by default), e.g. --log-level=2,supernode.task:INFO,supernode.server:DEBUG")
        ("log-console",   bpo::value<bool>(), "log to console. 1 or true or 0 or false. (true by default)")
#ifdef ELPP_SYSLOG
        ("log-file",      bpo::value<std::string>(), "log file; set it to syslog if you want use the syslog instead")
#else
        ("log-file",      bpo::value<std::string>(), "log file")
#endif
        ("log-format",    bpo::value<std::string>(), "e.g. %datetime{%Y-%M-%d %H:%m:%s.%g} %level %logger %rfile %msg");

    bpo::store(bpo::parse_command_line(argc, argv, desc), vm);
    bpo::notify(vm);

    if((exit = vm.count("help")))
        usage(desc);

    if(vm.count("config-file"))
        cfg.config_filename = vm["config-file"].as<std::string>();

    return exit;
}

void read_params_from_ini_file(Config& cfg, PTree& ini)
{
    if(cfg.config_filename.empty())
    {
        const Path self_path(cfg.self_dir);
        cfg.config_filename  = (self_path / "config.ini").string();
    }

    bpt::ini_parser::read_ini(cfg.config_filename, ini);
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

    const PTree& server_conf = ini.get_child("server");

    cfg.http_address              = server_conf.get<std::string>("http-address");
    cfg.coap_address              = server_conf.get<std::string>("coap-address");
    cfg.timer_poll_interval_ms    = server_conf.get<int>("timer-poll-interval-ms");
    cfg.http_connection_timeout   = server_conf.get<double>("http-connection-timeout");
    cfg.workers_count             = server_conf.get<int>("workers-count");
    cfg.worker_queue_len          = server_conf.get<int>("worker-queue-len");
    cfg.upstream_request_timeout  = server_conf.get<double>("upstream-request-timeout");
    cfg.lru_timeout_ms            = server_conf.get<int>("lru-timeout-ms");

    //configOpts.graftlet_dirs
    const PTree& graftlets_conf = ini.get_child("graftlets");
    boost::optional<std::string> gl_dirs = graftlets_conf.get_optional<std::string>("dirs");
    read_graftlet_dirs(cfg, gl_dirs ? gl_dirs.get() : "");

    const PTree& cryptonode_conf = ini.get_child("cryptonode");
    cfg.cryptonode_rpc_address = cryptonode_conf.get<std::string>("rpc-address");

    const PTree& log_conf = ini.get_child("logging");
    boost::optional<int> log_trunc_to_size  = log_conf.get_optional<int>("trunc-to-size");
    cfg.log_trunc_to_size = (log_trunc_to_size) ? log_trunc_to_size.get() : -1;

    const PTree& uri_subst = ini.get_child("upstream");
    std::for_each(uri_subst.begin(), uri_subst.end(), [&cfg, &uri_subst](const auto& it)
    {
        cfg.uri_subst.emplace_back(it.first);
        cfg.uri_subst.emplace_back(uri_subst.get<std::string>(it.first));
    });
}

void read_log_params(const PTree& ini_cfg, const VarMap& cl_cfg, Config& cfg)
{
    cfg.log_level = "3";
    cfg.log_console = true;

    read_log_params_from_ini(ini_cfg, cfg);
    read_log_params_from_cmd_line(cl_cfg, cfg);

    if(cfg.log_format.empty())
    {   // default log format (we need to explicitly apply it here, otherwise full path to a file will be logged  with monero default format)
        static const char* DEFAULT_LOG_FORMAT = "%datetime{%Y-%M-%d %H:%m:%s.%g}\t%thread\t%level\t%logger\t%rfile:%line\t%msg";
        cfg.log_format = DEFAULT_LOG_FORMAT;
    }
}

/*
void init_log(const PTree& ini_cfg, const VarMap& cl_cfg, Config& cfg)
{

//#ifdef ELPP_SYSLOG
//        if(log_filename == "syslog")
//        {
//            INITIALIZE_SYSLOG("graft_server");
//            mlog_syslog = true;
//            mlog_configure("", false, log_format.empty()? nullptr : log_format.c_str());
//        }
//        else
//#endif
//        {
//            mlog_configure(log_filename, log_console, log_format.empty()? nullptr : log_format.c_str());
//        }
//
//        mlog_set_log(log_level.c_str());
}
*/

ConfigLoader::ConfigLoader(void)
{
}

ConfigLoader::~ConfigLoader(void)
{
}

bool ConfigLoader::load(const int argc, const char** argv, Config& cfg)
{
    Path self_dir(argv[0]);
    self_dir = self_dir.remove_filename();
    cfg.self_dir = self_dir.string();

    VarMap cmd_line_params;
    if(read_params_from_cmd_line(argc, argv, cfg, cmd_line_params))
        return false;

    PTree ini_params;
    read_params_from_ini_file(cfg, ini_params);

    read_log_params(ini_params, cmd_line_params, cfg);

    return true;
}

void read_graftlet_dirs(Config& cfg, const std::string& dir_list)
{
    cfg.graftlet_dirs.clear();

    const Path self_dir(cfg.self_dir);
    if(dir_list.empty())
    {
        cfg.graftlet_dirs.push_back(fs::complete("graftlets", self_dir).string());
        return;
    }

    std::set<std::string> set;
    utils::split_string_by_separator(dir_list, ':', set);

    const Path cur_dir(fs::current_path());

    for(const auto& it : set)
    {
        const Path p(it);
        if(p.is_relative())
        {
            bool found = false;
            Path path1 = fs::complete(p, self_dir);
            if(fs::is_directory(path1))
            {
                cfg.graftlet_dirs.push_back(path1.string());
                found = true;
            }

            if(self_dir != cur_dir)
            {
                Path path2 = fs::complete(p, cur_dir);
                if(fs::is_directory(path2))
                {
                    cfg.graftlet_dirs.push_back(path2.string());
                    found = true;
                }
            }

            if(!found)
                LOG_PRINT_L1("Graftlet path '" << p.string() << "' is not a directory");
        }
        else
        {
            if(fs::is_directory(it))
                cfg.graftlet_dirs.emplace_back(p.string());
            else
                LOG_PRINT_L1("Graftlet path '" << p.string() << "' is not a directory");
        }
    }
    utils::remove_duplicates_from_vector(cfg.graftlet_dirs);
}

}

