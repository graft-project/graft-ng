#include "graft_manager.h"
#include "requests.h"

#include <misc_log_ex.h>
#include <boost/program_options.hpp>
#include <boost/property_tree/ini_parser.hpp>
// #include <boost/tokenizer.hpp>
#include <boost/filesystem.hpp>
#include <csignal>

namespace po = boost::program_options;
using namespace std;

namespace graft {
  void setCoapRouters(Manager& m);
  void setHttpRouters(Manager& m);
}

static std::function<void (int sig_num)> stop_handler;
static void signal_handler_stop(int sig_num)
{
    if(stop_handler) stop_handler(sig_num);
}

void addGlobalCtxCleaner(graft::Manager& manager, int ms)
{
    auto cleaner = [](const graft::Router::vars_t& vars, const graft::Input& input, graft::Context& ctx, graft::Output& output)->graft::Status
    {
        graft::Context::GlobalFriend::cleanup(ctx.global);
        return graft::Status::Ok;
    };
    manager.addPeriodicTask(
                graft::Router::Handler3(nullptr, cleaner, nullptr),
                std::chrono::milliseconds(ms)
                );
}

int main(int argc, const char** argv)
{
    std::signal(SIGINT, signal_handler_stop);
    std::signal(SIGTERM, signal_handler_stop);

    int log_level = 1;
    string config_filename;

    try {
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
            return 0;
        }

        if (vm.count("config-file")) {
            config_filename = vm["config-file"].as<string>();
        }
        if (vm.count("log-level")) {
            log_level = vm["log-level"].as<int>();
        }

        mlog_configure("", true);
        mlog_set_log_level(log_level);
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
        // [cryptonode]
        //  rpc-address <IP>:<PORT>
        //  p2p-address <IP>:<PORT> #maybe
        // [upstream]
        //  uri_name=uri_value #pairs for uri substitution
        //

        graft::ServerOpts sopts;

        const boost::property_tree::ptree& server_conf = config.get_child("server");
        sopts.http_address = server_conf.get<string>("http-address");
        sopts.coap_address = server_conf.get<string>("coap-address");
        sopts.timer_poll_interval_ms = server_conf.get<int>("timer-poll-interval-ms");
        sopts.http_connection_timeout = server_conf.get<double>("http-connection-timeout");
        sopts.workers_count = server_conf.get<int>("workers-count");
        sopts.worker_queue_len = server_conf.get<int>("worker-queue-len");
        sopts.upstream_request_timeout = server_conf.get<double>("upstream-request-timeout");
        sopts.data_dir = server_conf.get<string>("data-dir");
        int lru_timeout_ms = server_conf.get<int>("lru-timeout-ms");

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

        addGlobalCtxCleaner(manager, lru_timeout_ms);

        graft::setCoapRouters(manager);
        graft::setHttpRouters(manager);
        manager.enableRouting();

        {//check conflicts in routes
            std::string s = manager.dbgCheckConflictRoutes();
            if(!s.empty())
            {
                std::cout << std::endl << "==> manager.dbgDumpRouters()" << std::endl;
                std::cout << manager.dbgDumpRouters();

                //if you really need dump of r3tree uncomment two following lines
                //std::cout << std::endl << std::endl << "==> manager.dbgDumpR3Tree()" << std::endl;
                //manager.dbgDumpR3Tree();

                throw std::runtime_error("Routes conflict found:" + s);
            }
        }

        LOG_PRINT_L0("Starting server on: [http] " << sopts.http_address << ", [coap] " << sopts.coap_address);

        graft::GraftServer server;
        stop_handler = [&server](int sig_num)
        {
            LOG_PRINT_L0("Stoping server");
            server.stop();
        };
        server.serve(manager.get_mg_mgr());

    } catch (const std::exception & e) {
        std::cerr << "Exception thrown: " << e.what() << std::endl;
        return -1;
    }
    catch(...) {
        std::cerr << "Exception of unknown type!\n";
        return -1;
    }

    return 0;
}
