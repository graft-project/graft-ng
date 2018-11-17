
#include "supernode/config_loader.h"
#include "supernode/config.h"
#include "supernode/server/config_loader.h"

namespace graft::supernode {

ConfigLoader::ConfigLoader(void)
{
}

ConfigLoader::~ConfigLoader(void)
{
}

bool ConfigLoader::load(const int argc, const char** argv, Config& cfg)
{
    if(!server::ConfigLoader().load(argc, argv, cfg))
        return false;

    //VarMap cmd_line_params;
    //if(read_params_from_cmd_line(argc, argv, cfg, cmd_line_params))
    //    return false;

    //PTree ini_params;
    //read_params_from_ini_file(cfg, ini_params);

    //read_log_params(ini_params, cmd_line_params, cfg);

    return true;
}


/*
bool Node::initConfigOption(int argc, const char** argv, ConfigOpts& configOpts)
{
    bool res = Server::initConfigOption(argc, argv, configOpts);
    if(!res) return res;

    ConfigOptsEx& coptsex = static_cast<ConfigOptsEx&>(configOpts);
    assert(&m_configEx == &coptsex);

    boost::property_tree::ptree config;
    boost::property_tree::ini_parser::read_ini(m_cfg.config_filename, config);

    const boost::property_tree::ptree& server_conf = config.get_child("server");
    m_cfg.data_dir = server_conf.get<std::string>("data-dir");
    m_cfg.stake_wallet_name = server_conf.get<std::string>("stake-wallet-name", "stake-wallet");
    m_cfg.stake_wallet_refresh_interval_ms = server_conf.get<size_t>("stake-wallet-refresh-interval-ms",
                                                                      consts::DEFAULT_STAKE_WALLET_REFRESH_INTERFAL_MS);
    m_cfg.testnet = server_conf.get<bool>("testnet", false);
    return res;
}
*/

}

