
#include "supernode/route/router.h"

#include "supernode/route/handler3.h"
#include "supernode/route/route_set.h"
#include "supernode/route/job_params.h"

#include <string>
#include <map>
#include <iostream>
#include <sstream>

#include "r3.h"
//
//#include <utility>
//#include <algorithm>
//#include "inout.h"
//#include "context.h"

namespace graft::supernode::route {

Router::Router(void)
: m_compiled(false)
, m_node(nullptr)
{
    m_node = r3_tree_create(10);
}

Router::~Router(void)
{
    r3_tree_free(m_node);
}

bool Router::arm(void)
{
    std::for_each(m_routes.begin(), m_routes.end(), [this](RouteSet& rs)
    {
        std::for_each(rs.routes().begin(), rs.routes().end(), [this](Route& r)
            { r3_tree_insert_route(m_node, r.method, r.endpoint.c_str(), &r); });
    });

    char* err_str = nullptr;
    int err = r3_tree_compile(m_node, &err_str);

    if(err)
        std::cout << "error: " << std::string(err_str) << std::endl;

    return m_compiled = (err == 0);
}

void Router::add_route_set(RouteSet& rs)
{
    m_routes.push_front(std::move(rs));
}

bool Router::match(const std::string& target, const int method, JobParams& params) const
{
    bool ret = false;

    match_entry* entry = match_entry_create(target.c_str());
    entry->request_method = method;

    if(R3Route* m = r3_tree_match_route(m_node, entry))
    {
        for(size_t i = 0, cnt = entry->vars.tokens.size; i < cnt; ++i)
        {
            const auto& slug = entry->vars.slugs.entries[i];
            const auto& token = entry->vars.tokens.entries[i];

            params.vars.emplace(std::make_pair(
                std::move(std::string(slug.base, slug.len)),
                std::move(std::string(token.base, token.len))));
        }

        params.h3 = static_cast<Route*>(m->data)->h3;
        ret = true;
    }
    match_entry_free(entry);
    return ret;
}

std::string Router::dbg_dump_routes(void) const
{
    std::string res;
    int idx = 0;
    for(const RouteSet& rs : m_routes)
    {
        std::ostringstream ss;
        ss << "route_set[" << idx++ << "]->" << std::endl;
        res += ss.str();
        res += rs.dbg_dump("\t");
    }
    return res;
}

void Router::dbg_dump_R3Tree(const int level) const
{
    assert(m_compiled);
    r3_tree_dump(m_node, level);
}

std::string Router::dbg_check_conflict_routes(void) const
{
    std::map<std::string, int> map;
    for(const RouteSet& rs : m_routes)
    {
        for(const Route& r : rs.routes())
        {
            auto it = map.find(r.endpoint);
            if(it == map.end())
            {
                map[r.endpoint] = r.method;
                continue;
            }

            if(it->second & r.method)
                return r.endpoint;

            it->second &= r.method;
        }
    }
    return std::string();
}

}

