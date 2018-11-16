#pragma once

#include "supernode/server/server.h"
#include "supernode/config.h"

namespace graft::supernode {

using server::Server;

class Node : public Server
{
public:
    Node(void);
    ~Node(void);

    bool run(const Config& cfg);

protected:
    virtual void initMisc(server::Config& cfg) override;
    //virtual bool initConfigOption(int argc, const char** argv, ConfigOpts& configOpts) override;
    virtual void initRouters() override;

private:
    void prepareDataDirAndSupernodes();
    void startSupernodePeriodicTasks();
    void setHttpRouters(ConnectionManager& httpcm);
    void setCoapRouters(ConnectionManager& coapcm);

private:
    Config m_cfg;
};

}

