#include "router.h"

R3Node *Router::m_node = nullptr;

/*
int main()
{
	Router router1("/root1");
	Router router2("/root2");
	std::vector<std::pair<std::string, std::string>> vars;

	router1.addRoute("/r{id:\\d+}", METHOD_GET, test);
	router1.addRoute("/aaa/{s1}/bbb/{s2}", METHOD_GET, test);
	router2.addRoute("/rr{id:\\d+}", METHOD_GET, test);
	router2.addRoute("/aaaaaa/{s1}/bbbbbb/{s2}", METHOD_GET, test);

	if (router1.arm() && router2.arm()) {
		bool res = router1.match("/root1/r1", METHOD_GET, vars);
		      res |= router1.match("/root1/ra", METHOD_GET, vars);
		      res |= router2.match("/root2/aaaaaa/rr1/bbbbbb/rr2", METHOD_GET, vars);

		std::cout << res  << std::endl;
		for (auto const& p: vars)
			std::cout << p.first << ": " << p.second  << std::endl;
	}
}
*/
