
#include "supernode/route/route_set.h"

#include <iostream>
#include <sstream>
#include <assert.h>

namespace graft::supernode::route {

std::string method_to_str(const int method)
{
    constexpr const char* methpow[] = {"", "GET", "POST", "PUT", "DELETE", "PATCH", "HEAD", "OPTIONS"};
    assert((method & 0xFE) == method);

    std::string sm;
    for(unsigned int b = 1, idx = 0; idx < 8; b <<= 1, ++idx)
    {
        if(!(method & b))
            continue;

        if(!sm.empty())
            sm += '|';

        sm += methpow[idx];
    }
    return sm;
}

Route::Route(const std::string& end_poirnt, int method, const Handler3& h3)
: endpoint(end_poirnt)
, method(method)
, h3(h3)
{
}

RouteSet::RouteSet(const std::string& prefix)
: m_endpointPrefix(prefix)
{
}

RouteSet::RouteSet(RouteSet&& rs)
: m_routes(std::move(rs.m_routes))
, m_endpointPrefix(std::move(rs.m_endpointPrefix))
{
}

RouteSet::~RouteSet(void)
{
}

std::forward_list<Route> RouteSet::routes(void)
{
    return m_routes;
}

const std::forward_list<Route> RouteSet::routes(void) const
{
    return m_routes;
}

void RouteSet::add(const std::string& endpoint, const int method, const Handler3& ph3)
{
    m_routes.emplace_front(m_endpointPrefix + endpoint, method, ph3);
}

void RouteSet::add(const std::string& endpoint, const int method, const Handler3&& ph3)
{
    m_routes.emplace_front(m_endpointPrefix + endpoint, method, std::move(ph3));
}

std::string RouteSet::dbg_dump(const std::string& prefix) const
{
    std::ostringstream ss;
    for(const Route& r : m_routes)
    {
        auto ptr_to_str = [](const auto& ptr)->std::string
        {
            if(!ptr)
                return "nullptr";

            std::ostringstream ss;
            ss << &ptr;
            return ss.str();
        };

        const std::string sm = method_to_str(r.method);
        ss << prefix << sm << " " << r.endpoint << " (" <<
              ptr_to_str(r.h3.pre_action) << "," <<
              ptr_to_str(r.h3.worker_action) << "," <<
              ptr_to_str(r.h3.post_action) << ")" << std::endl;
    }
    return ss.str();
}

}

