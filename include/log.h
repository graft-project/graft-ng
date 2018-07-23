#pragma once

#include <string>
#include <misc_log_ex.h>

struct mg_mgr;
struct mg_connection;

namespace graft
{
    extern std::string client_addr(mg_connection* client);
} //namespace graft

#define LOG_PRINT_CLN(level,client,x) LOG_PRINT_L##level("[" << client_addr(client) << "]" << x)

#define LOG_PRINT_RQS_BT(level,bt,x) \
{ \
    ClientTask* cr = dynamic_cast<ClientTask*>(bt.get()); \
    if(cr) \
    { \
        LOG_PRINT_CLN(level,cr->m_client,x); \
    } \
    else \
    { \
        LOG_PRINT_L##level(x); \
    } \
}
