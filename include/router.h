#pragma once

#include <deque>
#include <functional>
#include <string>
#include <vector>
#include <utility>
#include <algorithm>
#include <iostream>

#include "r3.h"

namespace graft {

class Router
{
public:
	using vars_t = std::vector<std::pair<std::string, std::string>>;
	using Handler = std::function<bool (vars_t&, const std::string& , std::string& ) >;

	Router()
	{
		if (!m_node)
			m_node = r3_tree_create(10);
	}

	~Router()
	{
		if (m_node)
		{
			r3_tree_free(m_node);
			m_node = NULL;
		}
	}

	void addRoute(std::string endpoint, int methods, Handler* handler)
	{
		m_routes.push_back({endpoint, methods, handler});
	}

	bool arm()
	{
		std::for_each(m_routes.begin(), m_routes.end(),
				[this](Route& r)
				{
					r3_tree_insert_route(
						m_node,
						r.methods,
						r.endpoint.c_str(),
						reinterpret_cast<void*>(r.handler)
					);
				});

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
		std::string input;
		vars_t vars;
		Handler handler;
	};

	bool match(const std::string& target, int method, JobParams& params) const
	{
		match_entry *entry;
		R3Route *m;
		bool ret = false;

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

			params.handler = *reinterpret_cast<Handler*>(m->data);
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
		Handler* handler;
	};

	std::deque<Route> m_routes;

	static R3Node *m_node;
};

}//namespace graft
