#include "graft_manager.h"
#include "requests.h"

#include <boost/program_options.hpp>
#include <boost/property_tree/ini_parser.hpp>
// #include <boost/tokenizer.hpp>
#include <boost/filesystem.hpp>

namespace po = boost::program_options;
using namespace std;

namespace graft {
  void setCoapRouters(Manager& m);
  void setHttpRouters(Manager& m);
}

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


int main(int argc, const char** argv)
{
    string config_filename;

    po::variables_map vm;

    try {
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
            return 0;
        }

        if (vm.count("config-file")) {
            config_filename = vm["config-file"].as<string>();
        }

    }
    catch(exception& e) {
        cerr << "error: " << e.what() << "\n";
        return 1;
    }
    catch(...) {
        cerr << "Exception of unknown type!\n";
    }

    // load config
    boost::property_tree::ptree config;
    namespace fs = boost::filesystem;

    if (config_filename.empty()) {
        fs::path selfpath = argv[0];
        selfpath = selfpath.remove_filename();
        config_filename  = (selfpath / "config.ini").string();
    }

    try {
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

        init_log(config, vm);

        graft::ServerOpts sopts;

        const boost::property_tree::ptree& server_conf = config.get_child("server");
        sopts.http_address = server_conf.get<string>("http-address");
        sopts.coap_address = server_conf.get<string>("coap-address");
        sopts.timer_poll_interval_ms = server_conf.get<int>("timer-poll-interval-ms");
        sopts.http_connection_timeout = server_conf.get<double>("http-connection-timeout");
        sopts.workers_count = server_conf.get<int>("workers-count");
        sopts.worker_queue_len = server_conf.get<int>("worker-queue-len");
        sopts.upstream_request_timeout = server_conf.get<double>("upstream-request-timeout");

        const boost::property_tree::ptree& cryptonode_conf = config.get_child("cryptonode");
        sopts.cryptonode_rpc_address = cryptonode_conf.get<string>("rpc-address");
        //sopts.cryptonode_p2p_address = cryptonode_conf.get<string>("p2p-address");

        const boost::property_tree::ptree& uri_subst_conf = config.get_child("upstream");
        std::for_each(uri_subst_conf.begin(), uri_subst_conf.end(),[&uri_subst_conf](auto it)
        {
            std::string name(it.first);
            std::string val(uri_subst_conf.get<string>(name));
            graft::OutHttp::uri_substitutions.insert({std::move(name), std::move(val)});
        });

        graft::Manager manager(sopts);

        graft::setCoapRouters(manager);
        graft::setHttpRouters(manager);
        manager.enableRouting();

        graft::GraftServer server;

        LOG_PRINT_L0("Starting server on: [http] " << sopts.http_address << ", [coap] " << sopts.coap_address);
        server.serve(manager.get_mg_mgr());

    } catch (const std::exception & e) {
        std::cerr << "Exception thrown: " << e.what() << std::endl;
        return -1;
    }

    return 0;
}
