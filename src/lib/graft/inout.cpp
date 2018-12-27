
#include "lib/graft/inout.h"
#include "lib/graft/mongoosex.h"

namespace graft
{
std::unordered_map<std::string, std::tuple<std::string,int,bool,double>> OutHttp::uri_substitutions;

void InOutHttpBase::set_str_field(const http_message& hm, const mg_str& str_fld, std::string& fld)
{
    if(str_fld.len == 0)
    {
        fld.clear();
    }
    else
    {
        int off = str_fld.p - hm.message.p;
        assert(0<str_fld.len && 0<=off && off+str_fld.len<=hm.message.len);
        fld = std::string(str_fld.p, str_fld.len);
    }
}

InOutHttpBase& InOutHttpBase::operator = (const http_message& hm)
{
    //fill corresponding fields from hm
    set_str_field(hm, hm.body, body);
    set_str_field(hm, hm.method, method);
    set_str_field(hm, hm.uri, uri);
    set_str_field(hm, hm.proto, proto);
    resp_code = hm.resp_code;
    set_str_field(hm, hm.resp_status_msg, resp_status_msg);
    set_str_field(hm, hm.query_string, query_string);
    //fill headers from hm
    for(int i = 0; i < MG_MAX_HTTP_HEADERS; ++i)
    {
        const mg_str& h_n = hm.header_names[i];
        const mg_str& h_v = hm.header_values[i];
        assert((h_n.p == nullptr) == (h_n.len == 0));
        assert((h_v.p == nullptr) == (h_v.len == 0));
        assert(h_n.p != nullptr ||  h_v.p == nullptr);
        if(h_n.p == nullptr) break;
        headers.push_back({std::string(h_n.p, h_n.len), std::string(h_v.p, h_v.len)});
    }
    return *this;
}

std::string InOutHttpBase::combine_headers()
{
    std::string s = extra_headers;
    for(auto& pair : headers)
    {
        s += pair.first + ": " + pair.second + "\r\n";
    }
    return s;
}

std::string OutHttp::makeUri(const std::string& default_uri) const
{
    std::string uri_ = default_uri;

    std::string port_;
#define V(n) std::string n##_
        V(scheme); V(user_info); V(host); V(path); V(query); V(fragment);
#undef V
    while(!uri_.empty())
    {
        mg_str mg_uri{uri_.c_str(), uri_.size()};
        //[scheme://[user_info@]]host[:port][/path][?query][#fragment]
        unsigned int mg_port = 0;
        mg_str mg_scheme, mg_user_info, mg_host, mg_path, mg_query, mg_fragment;
        int res = mg_parse_uri(mg_uri, &mg_scheme, &mg_user_info, &mg_host, &mg_port, &mg_path, &mg_query, &mg_fragment);
        if(res<0) break;
        if(mg_port)
        port_ = std::to_string(mg_port);
#define V(n) n##_ = std::string(mg_##n.p, mg_##n.len)
        V(scheme); V(user_info); V(host); V(path); V(query); V(fragment);
#undef V
        break;
    }

    if(!proto.empty()) scheme_ = proto;
    if(!host.empty()) host_ = host;
    if(!port.empty()) port_ = port;
    if(!path.empty()) path_ = path;

    std::string url;
    if(!scheme_.empty())
    {
        url += scheme_ + "://";
        if(!user_info_.empty()) url += user_info_ + '@';
    }
    url += host_;
    if(!port_.empty()) url += ':' + port_;
    if(!path_.empty())
    {
        if(path_[0]!='/') path_ = '/' + path_;
        url += path_;
    }
    if(!query_.empty()) url += '?' + query_;
    if(!fragment_.empty()) url += '#' + fragment_;
    return url;
}

} //namespace graft

