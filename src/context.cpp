
#include "context.h"

#include "requestdefines.h"

namespace graft {

const ConfigOpts& Context::config_opts(void) const
{
    const ConfigOpts* p = global.get(CONTEXT_KEY_CONFIG_OPTS, (const ConfigOpts*)nullptr);
    assert(p);
    return *p;
}

void Context::config_opts(const ConfigOpts& copts)
{
    global[CONTEXT_KEY_CONFIG_OPTS] = &copts;
}

const Config& Context::config(void) const
{
    const Config* p = global.get(CONTEXT_KEY_CONFIG, (const Config*)nullptr);
    assert(p);
    return *p;
}

void Context::config(const Config& cfg)
{
    global[CONTEXT_KEY_CONFIG] = &cfg;
}

SysInfoCounter& Context::runtime_sys_info(void)
{
    SysInfoCounter* c = global.get(CONTEXT_KEY_RUNTIME_SYS_INFO, (SysInfoCounter*)nullptr);
    assert(c);
    return *c;
}

void Context::runtime_sys_info(SysInfoCounter& sic)
{
    global[CONTEXT_KEY_RUNTIME_SYS_INFO] = &sic;
}

}

