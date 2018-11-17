
#pragma once

#include "supernode/route/handler3.h"

#include <forward_list>
#include <string>

namespace graft::supernode::route {

struct Route
{
    std::string endpoint;
    int method;
    Handler3 h3;

    Route(const std::string& end_poirnt, int method, const Handler3& h3);
};

std::string method_to_str(int method);

class RouteSet
{
public:
    explicit RouteSet(const std::string& prefix = std::string());
    RouteSet(RouteSet&&);
    ~RouteSet();

    void add(const std::string& endpoint, int method, const Handler3& ph3);
    void add(const std::string& endpoint, int method, const Handler3&& ph3);

    std::string dbg_dump(const std::string& prefix = std::string()) const;

    std::forward_list<Route> routes(void);
    const std::forward_list<Route> routes(void) const;

private:
    std::forward_list<Route>  m_routes;
    std::string               m_endpointPrefix;
};

}
