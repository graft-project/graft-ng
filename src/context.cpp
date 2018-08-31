
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

SystemInfoProvider& Context::runtime_sys_info(void)
{
  SystemInfoProvider* p = global.get(CONTEXT_KEY_RUNTIME_SYS_INFO, (SystemInfoProvider*)nullptr);
  assert(p);
  return *p;
}

void Context::runtime_sys_info(SystemInfoProvider& sip)
{
  global[CONTEXT_KEY_RUNTIME_SYS_INFO] = &sip;
}

}



