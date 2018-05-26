#include "inout.h"
#include "mongoose.h"

namespace graft
{
std::unordered_map<std::string, std::string> OutHttp::uri_substitutions;

//following #define depends on hm such as const http_message&
#define SET_STR_FLD(fld) \
    { \
        if(hm.fld.len == 0) \
        { \
            fld.clear(); \
        } \
        else \
        { \
            int off = hm.fld.p - hm.message.p; \
            assert(0<hm.fld.len && 0<=off && off+hm.fld.len<=hm.message.len); \
            fld = std::string(hm.fld.p, hm.fld.len); \
        } \
    }
#define SET_ALL_FLD() \
    { \
        SET_STR_FLD(body); \
        SET_STR_FLD(method); \
        SET_STR_FLD(uri); \
        SET_STR_FLD(proto); \
        resp_code = hm.resp_code; \
        SET_STR_FLD(resp_status_msg); \
        SET_STR_FLD(query_string); \
    }
InOutHttpBase& InOutHttpBase::operator = (const http_message& hm)
{
    m_isHttp = true;
    SET_ALL_FLD();
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
#undef SET_ALL_FLD
#undef SET_STR_FLD

std::string InOutHttpBase::combine_headers()
{
    std::string s = extra_headers;
    for(auto& pair : headers)
    {
        s += pair.first + ": " + pair.second + "\r\n";
    }
    return s;
}

} //namespace graft
