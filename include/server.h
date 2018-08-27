#pragma once

#include <boost/program_options.hpp>
#include <boost/property_tree/ini_parser.hpp>

#include "connection.h"
#include "system_info.h"

namespace graft {

//namespace supernode { class SystmeInfoProvider; }

namespace po = boost::program_options;
namespace pt = boost::property_tree;

class GraftServer
{
public:
    bool run(int argc, const char** argv);
protected:
    virtual bool initConfigOption(int argc, const char** argv);
    virtual void intiConnectionManagers();
    virtual void override_config_values(pt::ptree& ini_data, po::variables_map& cmdline_data) { std::cout << "<<<<<<<<<" << std::endl; }
private:
    void initLog(int log_level);
    void initGlobalContext();
    void prepareDataDirAndSupernodes();
    void startSupernodePeriodicTasks();
    bool init(int argc, const char** argv);
    void serve();
    void stop(bool force = false) { m_looper->stop(force); }
    static void initSignals();
    void addGlobalCtxCleaner();
    void setHttpRouters(HttpConnectionManager& httpcm);
    void setCoapRouters(CoapConnectionManager& coapcm);
    static void checkRoutes(graft::ConnectionManager& cm);
    void create_system_info_provider(void);

    ConfigOpts m_configOpts;
    std::unique_ptr<graft::Looper> m_looper;
    std::vector<std::unique_ptr<graft::ConnectionManager>> m_conManagers;
    std::unique_ptr<graft::supernode::SystmeInfoProvider> m_sys_info;
};

}//namespace graft

