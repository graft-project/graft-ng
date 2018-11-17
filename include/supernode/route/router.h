
#pragma once

#include <forward_list>
#include <string>

struct _node;
typedef struct _node R3Node;

namespace graft::supernode::route {

struct JobParams;
struct RouteSet;

class Router
{
public:
    Router(void);
    ~Router(void);

    bool arm(void);
    bool match(const std::string& target, int method, JobParams& params) const;
    void add_route_set(RouteSet& rs);

    std::string dbg_dump_routes(void) const;
    void dbg_dump_R3Tree(int level = 0) const;
    std::string dbg_check_conflict_routes(void) const;

private:
    bool m_compiled;
    R3Node* m_node;
    std::forward_list<RouteSet> m_routes;
};

}

