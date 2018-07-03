#include "graft_manager.h"
#include "requests.h"
#include "requests/sendsupernodeannouncerequest.h"
#include "jsonrpc.h"
#include "rta/supernode.h"
#include "rta/fullsupernodelist.h"



#include <misc_log_ex.h>
#include <boost/program_options.hpp>
#include <boost/property_tree/ini_parser.hpp>
#include <boost/filesystem.hpp>
#include <csignal>


namespace po = boost::program_options;
using namespace std;

namespace consts {
   static const char * DATA_PATH = "supernode/data";
   static const char * STAKE_WALLET_PATH = "stake-wallet";
   static const char * WATCHONLY_WALLET_PATH = "stake-wallet";
   static const size_t DEFAULT_STAKE_WALLET_REFRESH_INTERFAL_MS = 5 * 1000;
}

namespace graft {
  void setCoapRouters(Manager& m);
  void setHttpRouters(Manager& m);
}

namespace tools {
    // TODO: make it crossplatform
    string getHomeDir()
    {
        return string(getenv("HOME"));
    }
}

static graft::GraftServer server;
static void signal_handler_stop(int sig_num)
{
    LOG_PRINT_L0("Stoping server");
    server.stop();
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


void startSupernodePeriodicTasks(graft::Manager& manager, size_t interval_ms)
{
    // update supernode every interval_ms
    auto supernodeRefreshWorker = [](const graft::Router::vars_t& vars, const graft::Input& input, graft::Context& ctx, graft::Output& output)->graft::Status
    {

        LOG_PRINT_L0("input: " << input.data());
        LOG_PRINT_L0("output: " << output.data());
        LOG_PRINT_L0("last status: " << (int)ctx.local.getLastStatus());

        switch (ctx.local.getLastStatus()) {
        case graft::Status::Forward: // reply from cryptonode
            return graft::Status::Ok;
        case graft::Status::Ok:
        case graft::Status::None:
            graft::FullSupernodeList::SupernodePtr supernode;

            supernode = ctx.global.get("supernode", graft::FullSupernodeList::SupernodePtr(nullptr));

            LOG_PRINT_L0("supernode: " << supernode.get());
            if (!supernode.get()) {
                LOG_ERROR("supernode is not set in global context");
                return graft::Status::Error;
            }
            supernode->refresh();

            LOG_PRINT_L0("supernode stake amount: " << supernode->stakeAmount());

            // TODO: replace call with "send_supernode_announce"
//            graft::JsonRpcRequestHeader req;
//            req.method = "get_info";
//            output.path = "/json_rpc";
//            output.load(req);

            graft::SupernodeAnnounce announce;
            supernode->prepareAnnounce(announce);
            graft::SendSupernodeAnnounceJsonRpcRequest req;
            req.params.push_back(announce);
            req.method = "send_supernode_announce";
            req.id = 0;
            output.load(req);
            output.path = "/json_rpc/rta";

            LOG_PRINT_L0("Calling cryptonode");
            return graft::Status::Forward;

        }

    };

    manager.addPeriodicTask(
                graft::Router::Handler3(nullptr, supernodeRefreshWorker, nullptr),
                std::chrono::milliseconds(interval_ms)
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
    }
    catch(exception& e) {
        cerr << "error: " << e.what() << "\n";
        return 1;
    }
    catch(...) {
        cerr << "Exception of unknown type!\n";
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


    try {
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


        graft::ServerOpts sopts;

        const boost::property_tree::ptree& server_conf = config.get_child("server");
        sopts.http_address = server_conf.get<string>("http-address");
        sopts.coap_address = server_conf.get<string>("coap-address");
        sopts.timer_poll_interval_ms = server_conf.get<int>("timer-poll-interval-ms");
        sopts.http_connection_timeout = server_conf.get<double>("http-connection-timeout");
        sopts.workers_count = server_conf.get<int>("workers-count");
        sopts.worker_queue_len = server_conf.get<int>("worker-queue-len");
        sopts.upstream_request_timeout = server_conf.get<double>("upstream-request-timeout");
        sopts.data_dir = server_conf.get<string>("data-dir", string());
        sopts.testnet =    server_conf.get<bool>("testnet", false);
        size_t stake_wallet_refresh_interval_ms = server_conf.get<size_t>("stake-wallet-refresh-interval-ms",
                                                                          consts::DEFAULT_STAKE_WALLET_REFRESH_INTERFAL_MS);

        if (sopts.data_dir.empty()) {
            boost::filesystem::path p = boost::filesystem::absolute(tools::getHomeDir());
            p /= ".graft/";
            p /= consts::DATA_PATH;
            sopts.data_dir = p.string();
        }

        int lru_timeout_ms = server_conf.get<int>("lru-timeout-ms");
        std::string stake_wallet_name = server_conf.get<string>("stake-wallet-name", "stake-wallet");

        const boost::property_tree::ptree& cryptonode_conf = config.get_child("cryptonode");
        sopts.cryptonode_rpc_address = cryptonode_conf.get<string>("rpc-address");
        // sopts.cryptonode_p2p_address = cryptonode_conf.get<string>("p2p-address");

        const boost::property_tree::ptree& uri_subst_conf = config.get_child("upstream");
        std::for_each(uri_subst_conf.begin(), uri_subst_conf.end(),[&uri_subst_conf](auto it)
        {
            std::string name(it.first);
            std::string val(uri_subst_conf.get<string>(name));
            graft::OutHttp::uri_substitutions.insert({std::move(name), std::move(val)});
        });

        graft::Manager manager(sopts);


        addGlobalCtxCleaner(manager, lru_timeout_ms);

        // create data directory if not exists
        boost::filesystem::path data_path(sopts.data_dir);
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


        // create supernode instance and put it into global context
        graft::FullSupernodeList::SupernodePtr supernode {new graft::Supernode(
                        (stake_wallet_path / stake_wallet_name).string(),
                        "", // TODO
                        sopts.cryptonode_rpc_address,
                        sopts.testnet
                        )};

        supernode->setNetworkAddress(sopts.http_address + "/dapi/v2.0");

        manager.get_gcm().addOrUpdate("supernode", supernode);

        // create fullsupernode list instance and put it into global context
        LOG_PRINT_L0(sopts.cryptonode_rpc_address);
        boost::shared_ptr<graft::FullSupernodeList> fsl {new graft::FullSupernodeList(sopts.cryptonode_rpc_address, sopts.testnet)};
        manager.get_gcm().addOrUpdate("fsl", fsl);

        graft::setCoapRouters(manager);
        graft::setHttpRouters(manager);

        startSupernodePeriodicTasks(manager, stake_wallet_refresh_interval_ms);

        manager.enableRouting();

        LOG_PRINT_L0("Starting server on: [http] " << sopts.http_address << ", [coap] " << sopts.coap_address);
        server.serve(manager.get_mg_mgr());

    } catch (const std::exception & e) {
        std::cerr << "Exception thrown: " << e.what() << std::endl;
        return -1;
    }

    return 0;
}
