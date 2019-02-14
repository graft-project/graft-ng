
#include "lib/graft/mongoosex.h"

extern "C" {

mg_connection *mg_connect_http_base(
    mg_mgr *mgr, MG_CB(mg_event_handler_t ev_handler, void *user_data),
    mg_connect_opts opts, const char *scheme1, const char *scheme2,
    const char *scheme_ssl1, const char *scheme_ssl2, const char *url,
    mg_str *path, mg_str *user_info, mg_str *host);

} //extern "C"

namespace mg
{

mg_connection *mg_send_http_opt_x(mg_connection *nc,
    mg_mgr *mgr, MG_CB(mg_event_handler_t ev_handler, void *user_data),
    mg_connect_opts opts, const char *url, const char *extra_headers,
    const std::string& post_data)
{
    mg_str user = MG_NULL_STR, null_str = MG_NULL_STR;
    mg_str host = MG_NULL_STR, path = MG_NULL_STR;
    mbuf auth;
    if(nc == NULL)
    {
        nc = mg_connect_http_base(
                    mgr, MG_CB(ev_handler, user_data),
                    opts, "http", NULL,
                    "https", NULL, url,
                    &path, &user, &host);

        if (nc == NULL) return NULL;
    }
    else
    {
        //TODO: excessive parse second time
        mg_parse_uri(mg_mk_str(url), NULL, &user, &host, NULL, &path, NULL, NULL);
        nc = nc;
    }

    mbuf_init(&auth, 0);
    if (user.len > 0)
    {
        mg_basic_auth_header(user, null_str, &auth);
    }

    if (extra_headers == NULL) extra_headers = "";
    if (path.len == 0) path = mg_mk_str("/");
    if (host.len == 0) host = mg_mk_str("");

    mg_printf(nc, "%s %.*s HTTP/1.1\r\nHost: %.*s\r\nContent-Length: %" SIZE_T_FMT
              "\r\n%.*s%s\r\n",
              (post_data.empty() ? "GET" : "POST"), (int) path.len, path.p,
              (int) (path.p - host.p), host.p, post_data.size(), (int) auth.len,
              (auth.buf == NULL ? "" : auth.buf), extra_headers);

    mg_send(nc, post_data.c_str(), post_data.size());

    mbuf_free(&auth);
    return nc;
}

mg_connection *mg_connect_http_x(mg_connection *nc,
    mg_mgr *mgr, MG_CB(mg_event_handler_t ev_handler, void *user_data),
    const char *url, const char *extra_headers, const std::string& post_data)
{
    mg_connect_opts opts;
    memset(&opts, 0, sizeof(opts));
    return mg_send_http_opt_x(nc, mgr, MG_CB(ev_handler, user_data),
                                 opts, url, extra_headers,
                                 post_data);
}

} //namespace mg
