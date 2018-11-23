
#include "lib/graft/router.h"

namespace graft
{

template<typename In, typename Out>
bool RouterT<In,Out>::Root::arm()
{
    std::for_each(m_routers.begin(), m_routers.end(),
        [this](Router& ro)
        {
            std::for_each(ro.m_routes.begin(), ro.m_routes.end(),
                [this](Route& r)
                {
                    r3_tree_insert_route(m_node, r.methods, r.endpoint.c_str(), &r);
                }
            );
        }
    );
    char *errstr = NULL;
    int err = r3_tree_compile(m_node, &errstr);

    if (err != 0)
        std::cout << "error: " << std::string(errstr) << std::endl;

    return m_compiled = (err == 0);
}

template<typename In, typename Out>
bool RouterT<In,Out>::Root::match(const std::string& target, int method, JobParams& params)
{
    bool ret = false;

    match_entry *entry = match_entry_create(target.c_str());
    entry->request_method = method;

    R3Route *m = r3_tree_match_route(m_node, entry);
    if (m)
    {
        for (size_t i = 0; i < entry->vars.tokens.size; i++)
            params.vars.emplace(std::make_pair(
                std::move(std::string(entry->vars.slugs.entries[i].base, entry->vars.slugs.entries[i].len)),
                std::move(std::string(entry->vars.tokens.entries[i].base, entry->vars.tokens.entries[i].len))
            ));

        params.h3 = static_cast<Route*>(m->data)->h3;
        ret = true;
    }
    match_entry_free(entry);
    return ret;
}

template<typename In, typename Out>
std::string RouterT<In,Out>::Root::dbgDumpRouters() const
{
    std::string res;
    int idx = 0;
    for(const RouterT& r : m_routers)
    {
        std::ostringstream ss;
        ss << "router[" << idx++ << "]->" << std::endl;
        res += ss.str();
        res += r.dbgDumpRouter("\t");
    }
    return res;
}

template<typename In, typename Out>
void RouterT<In,Out>::Root::dbgDumpR3Tree(int level) const
{
    assert(m_compiled);
    r3_tree_dump(m_node, level);
}

template<typename In, typename Out>
std::string RouterT<In,Out>::Root::dbgCheckConflictRoutes() const
{
    //route -> method bits
    std::map<std::string,int> map;
    for(const RouterT& r : m_routers)
    {
        for(const Route& rr : r.m_routes)
        {
            auto it = map.find(rr.endpoint);
            if(it == map.end())
            {
                map[rr.endpoint] = rr.methods;
                continue;
            }
            if(it->second & rr.methods) return rr.endpoint;
            it->second &= rr.methods;
        }
    }
    return std::string();
}

template<typename In, typename Out>
std::string RouterT<In,Out>::dbgDumpRouter(const std::string prefix) const
{
    std::ostringstream ss;
    for(const Route& r : m_routes)
    {
        std::string sm = methodsToString(r.methods);
        auto ptrs = [](auto& ptr)->std::string
        {
            if(ptr == nullptr) return "nullptr";
            std::ostringstream ss;
            ss << &ptr;
            return ss.str();
        };
        ss << prefix << sm << " " << r.endpoint << " (" <<
              ptrs(r.h3.pre_action) << "," <<
              ptrs(r.h3.worker_action) << "," <<
              ptrs(r.h3.post_action) << ")" << std::endl;
    }
    return ss.str();
}

template class RouterT<Input, Output>;

template<typename In, typename Out>
std::string RouterT<In, Out>::methodsToString(int methods)
{
    constexpr const char* methpow[] = {"", "GET", "POST", "PUT", "DELETE", "PATCH", "HEAD", "OPTIONS"};
    assert((methods&0xFE)==methods);
    std::string sm;
    for(unsigned int b=1, idx=0; idx<8; b<<=1, ++idx)
    {
        if(!(methods&b)) continue;
        if(!sm.empty()) sm += '|';
        sm += methpow[idx];
    }
    return sm;
}

}//namespace graft

