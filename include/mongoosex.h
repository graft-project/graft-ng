#pragma once

#include "mongoose.h"

#include <string>

namespace mg
{
struct mg_connection *mg_connect_http_opt_x(
    struct mg_mgr *mgr, MG_CB(mg_event_handler_t ev_handler, void *user_data),
    struct mg_connect_opts opts, const char *url, const char *extra_headers,
    const std::string& post_data);

struct mg_connection *mg_connect_http_x(
    struct mg_mgr *mgr,
    MG_CB(mg_event_handler_t event_handler, void *user_data), const char *url,
    const char *extra_headers, const std::string& post_data);

}
