#include "graft_manager.h"

#include <boost/program_options.hpp>
#include <boost/property_tree/ini_parser.hpp>
// #include <boost/tokenizer.hpp>
#include <boost/filesystem.hpp>

namespace po = boost::program_options;
using namespace std;

int main(int argc, const char** argv)
{
    int log_level = 1;
    string config_filename;

    try {
        po::options_description desc("Allowed options");
        desc.add_options()
                ("help", "produce help message")
                ("config-file", po::value<string>(), "config filename (config.ini by default")
                ("log-level", po::value<int>(), "log-level. 3 by default)");

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
        // -------------------------------- DAPI -------------------------------------------
        const boost::property_tree::ptree& dapi_conf = config.get_child("dapi");
        // use DAPI version
        // supernode::rpc_command::SetDAPIVersion( dapi_conf.get<string>("version") );

        // use IP, port and threads
        // dapi_server.Set( dapi_conf.get<string>("ip"), dapi_conf.get<string>("port"), dapi_conf.get<int>("threads") );

        // use wallet_proxy_only
        // supernode::rpc_command::SetWalletProxyOnly( dapi_conf.get<int>("wallet_proxy_only", 0)==1 );



        // -------------------------------- Servant -----------------------------------------

        const boost::property_tree::ptree& cf_ser = config.get_child("servant");
        // use "bdb_path", "daemon_addr"
        // supernode::FSN_Servant* servant = new supernode::FSN_Servant_Test( cf_ser.get<string>("bdb_path"), cf_ser.get<string>("daemon_addr"), "", cf_ser.get<bool>("is_testnet") );
//        if( !supernode::rpc_command::IsWalletProxyOnly() ) {
//          servant->Set( cf_ser.get<string>("stake_wallet_path"), "", cf_ser.get<string>("miner_wallet_path"), "");
//          // TODO: Remove next code, it only for testing
//          const boost::property_tree::ptree& fsn_hardcoded = config.get_child("fsn_hardcoded");
//          for(unsigned i=1;i<10000;i++) {
//            string key = string("data")+boost::lexical_cast<string>(i);
//            string val = fsn_hardcoded.get<string>(key, "");
//            if(val=="") break;
//            vector<string> vv = supernode::helpers::StrTok(val, ":");

//            servant->AddFsnAccount(boost::make_shared<supernode::FSN_Data>(supernode::FSN_WalletData{vv[2], vv[3]}, supernode::FSN_WalletData{vv[4], vv[5]}, vv[0], vv[1]));
//          }
//          // TODO: end
//        }//if wallet proxy only


    } catch (const std::exception & e) {
        // LOG_ERROR("exception thrown: " << e.what());
        return -1;
    }
    // TODO: start app here


    return 0;
}

