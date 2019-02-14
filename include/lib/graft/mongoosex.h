#pragma once

#include "mongoose.h"

#include <string>

//Following functions are adapted from similar mongoose functions. They allow sending of null chars,
//for this they take const std::string& as some parameters instead of const char*.

namespace mg
{

mg_connection *mg_connect_http_x(
    mg_connection *nc,
    mg_mgr *mgr,
    MG_CB(mg_event_handler_t event_handler, void *user_data), const char *url,
    const char *extra_headers, const std::string& post_data);

} //namespace mg
