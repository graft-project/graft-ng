#pragma once

#include <deque>
#include <functional>
#include <string>
#include <vector>
#include <utility>
#include <algorithm>
#include <iostream>

#include "r3.h"
#include "inout.h"
#include "context.h"

namespace graft {

template<typename In, typename Out>
class RouterT
{
public:
    enum class Status
    {
        Ok,
        Forward,
        Error,
        Drop,
        None,
    };

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

        Handler3(const Handler& pre, const Handler& peri, const Handler& post)
            : pre(pre), peri(peri), post(post)
        { }
        Handler3(Handler&& pre, Handler&& peri, Handler&& post)
            : pre(std::move(pre)), peri(std::move(peri)), post(std::move(post))
        { }

        Handler3(const Handler& peri) : peri(peri) { }
        Handler3(Handler&& peri) : peri(std::move(peri)) { }
    public:
        Handler pre;
        Handler peri;
        Handler post;
    };

    RouterT(const std::string& prefix = "") : m_endpointPrefix(prefix)
    {
        if (!m_node)
            m_node = r3_tree_create(10);
    }

    ~RouterT()
    {
        if (m_node)
        {
            r3_tree_free(m_node);
            m_node = NULL;
        }
    }

    void addRoute(std::string endpoint, int methods, Handler3* ph3)
    {
        m_routes.push_back({m_endpointPrefix + endpoint, methods, ph3});
    }

    bool arm()
    {
        std::for_each(m_routes.begin(), m_routes.end(),
            [this](Route& r)
            {
                r3_tree_insert_route(m_node, r.methods, r.endpoint.c_str(), reinterpret_cast<void*>(r.h3));
            }
        );

        char *errstr = NULL;
        int err = r3_tree_compile(m_node, &errstr);

        if (err != 0)
            std::cout << "error: " << std::string(errstr) << std::endl;
        else
            r3_tree_dump(m_node, 0);

        return (err == 0);
    }

    struct JobParams
    {
        Input input;
        vars_t vars;
        Handler3 h3;
    };

    static bool match(const std::string& target, int method, JobParams& params)
    {
        match_entry *entry;
        R3Route *m;
        bool ret = false;

        if (!m_node) return false;

        entry = match_entry_create(target.c_str());
        entry->request_method = method;

        m = r3_tree_match_route(m_node, entry);
        if (m)
        {
            for (size_t i = 0; i < entry->vars.tokens.size; i++)
                params.vars.emplace_back(std::make_pair(
                    std::move(std::string(entry->vars.slugs.entries[i].base, entry->vars.slugs.entries[i].len)),
                    std::move(std::string(entry->vars.tokens.entries[i].base, entry->vars.tokens.entries[i].len))
                ));

            params.h3 = *reinterpret_cast<Handler3*>(m->data);
            ret = true;
        }
        match_entry_free(entry);

        return ret;
    }

private:
    struct Route
    {
        std::string endpoint;
        int methods;
        Handler3* h3;
    };

    std::deque<Route> m_routes;

    static R3Node *m_node;
    std::string m_endpointPrefix;
};

template<typename In, typename Out>
R3Node *RouterT<In, Out>::m_node = nullptr;

}//namespace graft
