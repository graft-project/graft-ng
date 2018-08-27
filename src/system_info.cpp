#include "system_info.h"

namespace graft { namespace supernode {

SystmeInfoProvider::SystmeInfoProvider(void)
: http_req_total_cnt_(0)
, http_req_routed_cnt_(0)
, http_req_unrouted_cnt_(0)
{
}

SystmeInfoProvider::~SystmeInfoProvider(void)
{
}

} }

