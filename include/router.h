#pragma once

#include <forward_list>
#include <functional>
#include <string>
#include <map>
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
    using vars_t = std::multimap<std::string, std::string>;
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

        bool arm();
        bool match(const std::string& target, int method, JobParams& params);
        void addRouter(RouterT& r) { m_routers.push_front(std::move(r)); }

        std::string dbgDumpRouters() const;
        void dbgDumpR3Tree(int level = 0) const;
        std::string dbgCheckConflictRoutes() const;
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
    std::string dbgDumpRouter(const std::string prefix = "") const;

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
