
#include "common/utils.h"
#include "connection.h"
#include "string_coding.h"

#include <locale>            // epee::string_coding uses std::locale but misses include
#include <boost/algorithm/string/trim.hpp>

namespace graft::supernode::utils {

std::string base64_decode(const std::string& encoded_data)
{
    return epee::string_encoding::base64_decode(encoded_data);
}

std::string base64_encode(const std::string& data)
{
    return epee::string_encoding::base64_encode(data);
}

std::string get_home_dir(void)
{
    return std::string(getenv("HOME"));
}

std::string trim_comments(std::string s)  //remove ;; tail
{
    const std::size_t pos = s.find(";;");

    if(pos != std::string::npos)
      s = s.substr(0, pos);

    boost::trim_right(s);
    return s;
}

void check_routes(graft::ConnectionManager& cm)
{//check conflicts in routes
    std::string s = cm.dbgCheckConflictRoutes();
    if(!s.empty())
    {
        std::cout << std::endl << "==> " << cm.getProto() << " manager.dbgDumpRouters()" << std::endl;
        std::cout << cm.dbgDumpRouters();

        //if you really need dump of r3tree uncomment two following lines
        //std::cout << std::endl << std::endl << "==> manager.dbgDumpR3Tree()" << std::endl;
        //manager.dbgDumpR3Tree();

        throw std::runtime_error("Routes conflict found:" + s);
    }
}

void split_string_by_separator(const std::string& src, const char sep, std::set<std::string>& dst)
{
    for(std::string::size_type s = 0;;)
    {
        std::string::size_type e = src.find(sep, s);
        if(e == std::string::npos)
        {
            dst.insert(src.substr(s));
            break;
        }
        dst.insert(src.substr(s, e - s));
        s = e + 1;
    }
}

void remove_duplicates_from_vector(std::vector<std::string>& vec)
{
    std::set<std::string> set;
    auto end = std::remove_if(vec.begin(), vec.end(), [&set](const auto& s)->bool { return !set.emplace(s).second; });
    vec.erase(end, vec.end());
}

}

