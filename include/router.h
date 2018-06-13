#pragma once

#include <forward_list>
#include <functional>
#include <string>
#include <vector>
#include <utility>
#include <algorithm>
#include <iostream>
#include <sstream>

#include "r3.h"
#include "inout.h"
#include "context.h"

namespace graft {

template<typename In, typename Out>
class RouterT
{
public:
    using vars_t = std::vector<std::pair<std::string, std::string>>;
    using Handler = std::function<Status (const vars_t&, const In&, Context&, Out& ) >;

    struct Handler3
    {
        Handler3() = default;

        Handler3(const Handler3&) = default;
        Handler3(Handler3&&) = default;
        Handler3& operator = (const Handler3&) = default;
        Handler3& operator = (Handler3&&) = default;
        ~Handler3() = default;

        Handler3(const Handler& pre_action, const Handler& action, const Handler& post_action)
            : pre_action(pre_action), worker_action(action), post_action(post_action)
        { }
        Handler3(Handler&& pre_action, Handler&& action, Handler&& post_action)
            : pre_action(std::move(pre_action)), worker_action(std::move(action)), post_action(std::move(post_action))
        { }

        Handler3(const Handler& worker_action) : worker_action(worker_action) { }
        Handler3(Handler&& worker_action) : worker_action(std::move(worker_action)) { }
    public:
        Handler pre_action;
        Handler worker_action;
        Handler post_action;
    };

    struct JobParams
    {
        Input input;
        vars_t vars;
        Handler3 h3;
    };

    class Root
    {
    public:
        Root() { m_node = r3_tree_create(10); }
        ~Root() { r3_tree_free(m_node); }

        bool arm()
        {
            std::for_each(m_routers.begin(), m_routers.end(),
                [this](RouterT<In, Out>& ro)
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

        bool match(const std::string& target, int method, JobParams& params)
        {
            bool ret = false;

            match_entry *entry = match_entry_create(target.c_str());
            entry->request_method = method;

            R3Route *m = r3_tree_match_route(m_node, entry);
            if (m)
            {
                for (size_t i = 0; i < entry->vars.tokens.size; i++)
                    params.vars.emplace_back(std::make_pair(
                        std::move(std::string(entry->vars.slugs.entries[i].base, entry->vars.slugs.entries[i].len)),
                        std::move(std::string(entry->vars.tokens.entries[i].base, entry->vars.tokens.entries[i].len))
                    ));

                params.h3 = static_cast<Route*>(m->data)->h3;
                ret = true;
            }
            match_entry_free(entry);
            return ret;
        }

        void addRouter(RouterT& r) { m_routers.push_front(std::move(r)); }

    public:
        std::string dbgDumpRouters() const
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

        void dbgDumpR3Tree(int level = 0) const
        {
            assert(m_compiled);
            r3_tree_dump(m_node, level);
        }

        std::string dbgCheckConflictRoutes() const
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
        }

    private:
        bool m_compiled = false;
        R3Node *m_node;
        std::forward_list<RouterT> m_routers;
    };

    RouterT(const std::string& prefix = std::string()) : m_endpointPrefix(prefix) { }

    RouterT(RouterT&& r)
        : m_endpointPrefix(std::move(r.m_endpointPrefix))
        , m_routes(std::move(r.m_routes)) {}

    ~RouterT() = default;

    void addRoute(const std::string& endpoint, int methods, const Handler3& ph3)
    {
        Route r{m_endpointPrefix + endpoint, methods, ph3};
        m_routes.push_front(r);
    }

    void addRoute(const std::string& endpoint, int methods, const Handler3&& ph3)
    {
        m_routes.push_front({m_endpointPrefix + endpoint, methods, std::move(ph3)});
    }

public:
    std::string dbgDumpRouter(const std::string prefix = "") const
    {
        constexpr const char* methpow[] = {"", "GET", "POST", "PUT", "DELETE", "PATCH", "HEAD", "OPTIONS"};
        std::ostringstream ss;
        for(const Route& r : m_routes)
        {
            assert((r.methods&0xFE)==r.methods);
            std::string sm;
            for(unsigned int b=1, idx=0; idx<8; b<<=1, ++idx)
            {
                if(!(r.methods&b)) continue;
                if(!sm.empty()) sm += '|';
                sm += methpow[idx];
            }
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
private:
    struct Route
    {
        std::string endpoint;
        int methods;
        Handler3 h3;
    };

    std::forward_list<Route> m_routes;
    std::string m_endpointPrefix;

    friend bool Root::arm();
};

using Router = RouterT<Input, Output>;

}//namespace graft
