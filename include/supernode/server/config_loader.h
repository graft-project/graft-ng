
#pragma once

namespace graft::supernode::server {

class Config;

class ConfigLoader
{
public:
    ConfigLoader(void);
    ~ConfigLoader(void);

    ConfigLoader(const ConfigLoader&) = delete;
    ConfigLoader& operator = (const ConfigLoader&) = delete;

    bool load(int argc, const char** argv, Config& cfg);
};

}

