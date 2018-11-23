/*
 * Copyright (c) 2014 Clark Cianfarini
 * Copyright (c) 2018 The Graft Project
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this software
 * and associated documentation files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or
 * substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
 * BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <tuple>
#include <regex>
#include <misc_log_ex.h>
#include "lib/graft/graft_exception.h"
#define INCLUDE_DEPENDENCY_GRAPH
#include "lib/graft/GraftletLoader.h"

#undef MONERO_DEFAULT_LOG_CATEGORY
#define MONERO_DEFAULT_LOG_CATEGORY "graftlet.GraftletLoader"

namespace graftlet
{

GraftletLoader::ExceptionMap GraftletLoader::m_exceptionMap;
GraftletLoader::Version GraftletLoader::m_fwVersion = GRAFTLET_MKVER(1,0);

void GraftletLoader::setGraftletsExceptionList(const GraftletExceptionList& gel)
{
    m_exceptionMap.clear();
    for(auto& row : gel)
    {
        const DllName& dllName = row.first;
        const ExceptionRngVec& vec = row.second;

        auto res = m_exceptionMap.try_emplace(dllName, vec);
        if(!res.second)
            throw std::runtime_error("In the graftlet exception list, dll name '" + dllName + "' is duplicated.");
    }
}

void GraftletLoader::findGraftletsInDirectory(std::string directory, std::string extension)
{
    namespace fs = boost::filesystem;
    namespace dll = boost::dll;

    for(fs::directory_iterator it(directory), eit; it != eit; ++it)
    {
        if(fs::is_directory(*it)) continue;

        if(it->path().extension() != "."+extension) continue;

        LOG_PRINT_L2("." << extension << " library found:'" << it->path().c_str() << "'");

        bool loaded = false;
        bool called = false;
        try
        {
            dll::shared_library lib(it->path(), dll::load_mode::append_decorations);
            loaded = true;
#define CHECK(func) \
    if(!lib.has(func)) \
    { \
        LOG_PRINT_L2(func) "not found '" << it->path().c_str() << "'"; \
        continue; \
    }

            CHECK("getBuildSignature");
            CHECK("getGraftletName");
            CHECK("getGraftletVersion");
            CHECK("getGraftletRegistry");
#undef CHECK
            auto getGraftletABI = dll::import<decltype(getBuildSignature)>(lib, "getBuildSignature");
            std::string graftletABI = getGraftletABI();
            if(graftletABI != getBuildSignature())
            {
                LOG_PRINT_L2("\tgraftlet ABI does not match '") << graftletABI << "' != '" << getBuildSignature() << "'";
                continue;
            }

            auto graftletRegistryAddr = dll::import<decltype(getGraftletRegistry)>(lib, "getGraftletRegistry" );
            GraftletRegistry* graftletRegistry = graftletRegistryAddr();

            auto graftletFileVersion = dll::import<decltype(getGraftletVersion)>(lib, "getGraftletVersion");
            int graftletVersion = graftletFileVersion();

            auto getGraftletNameFunc = dll::import<decltype(getGraftletName)>(lib, "getGraftletName");
            DllName dllName = getGraftletNameFunc();

            Dependencies dependencies = "";
            if(lib.has("getGraftletDependencies"))
            {
                auto getGraftletDependenciesFunc = dll::import<decltype(getGraftletDependencies)>(lib, "getGraftletDependencies");
                dependencies = getGraftletDependenciesFunc();
            }

            called = true;

            DllPath dll_path = it->path().c_str();

            if(is_in_GEL(dllName, graftletVersion))
            {
                LOG_PRINT_L2("The graftlet '") << dllName << "', version " << getVersionStr(graftletVersion) << " is in the exception list";
                continue;
            }

            //check version in graftlet
            auto checkFwVersionFunc = dll::import<decltype(checkFwVersion)>(lib, "checkFwVersion" );
            if(!checkFwVersionFunc(m_fwVersion))
            {
                LOG_PRINT_L2("The graftlet '") << dllName << "', version " << getVersionStr(graftletVersion) << " is not compatible with current version "
                                               << getVersionStr(m_fwVersion);
                continue;
            }

            LOG_PRINT_L2("The graftlet accepted '") << dllName << " version " << graftletVersion << " path " << dll_path;

            auto res = m_name2lib.emplace(
                        std::make_pair(dllName,
                                       std::make_tuple( std::move(lib), graftletVersion, std::move(dll_path), std::move(dependencies) ))
                        );
            if(!res.second) throw std::runtime_error("A plugin with the name '" + dllName + "' already exists");
            auto res1 = m_name2registries.emplace( std::make_pair(dllName, graftletRegistry) );
            assert(res1.second);
        }
        catch(std::exception& ex)
        {
            if(!loaded)
            {
                LOG_PRINT_L2("Cannot load library: '" << it->path() << "' because '" << ex.what() << "'");
                continue;
            }
            else if(called)
            {
                LOG_PRINT_L2("An exception occurred, most probably during call of one of mandatory functions of graftlet,"
                             " such as getGraftletVersion, the graftlet: '" << it->path() << "' because '" << ex.what() << "'");
                continue;
            }
            else throw;
        }
    }
}

void GraftletLoader::DependencyGraph::initialize(const std::vector<std::tuple<DllName,Version,Dependencies>>& vec)
{
    m_graph.clear();
    m_dll2ver.clear();
    //initialize graph
    for(auto& item : vec)
    {
        const DllName& dllName = std::get<0>(item);
        const Version& ver = std::get<1>(item);
        std::string_view deps = std::get<2>(item); //Dependencies
        //make list of dependencies
        std::list<std::pair<DllName,Version>> list;
        if(!deps.empty())
        {
            for(std::string::size_type s = 0;;)
            {
                std::string::size_type e = deps.find(',',s);
                std::string_view nv = (e == std::string::npos)? deps.substr(s) : deps.substr(s,e-s);

                std::string snv{nv};
                std::regex regex(R"(^\s*([^:\s]*)\s*(:\s*([0-9]+)\s*(\.\s*([0-9]+))?)?\s*$)");
                std::smatch m;
                if(!std::regex_match(snv, m, regex))
                {//invalid format of dependencies, the dll should be removed later
                    list.clear();
                    list.emplace_back(std::make_pair("",0));
                    break;
                }
                assert(1 < m.size());
                std::string name = m[1];
                if(name.empty())
                {//invalid format
                    list.clear();
                    list.emplace_back(std::make_pair("",0));
                    break;
                }
                Version minver = 0;
                if(2 < m.size() && m[2].matched)
                {
                    assert(3 < m.size() && m[3].matched);
                    int Ma = std::stoi(m[3]);
                    int mi = 0;
                    if(4 < m.size() && m[4].matched)
                    {
                        assert(5 < m.size() && m[5].matched);
                        mi = std::stoi(m[5]);
                    }
                    minver = GRAFTLET_MKVER(Ma, mi);
                }

                list.push_back(std::make_pair(DllName(name),minver));

                if(e == std::string::npos) break;
                s = e + 1;
            }
        }

        auto res = m_graph.emplace(dllName, std::move(list));
        assert(res.second);
        auto res1 = m_dll2ver.emplace(dllName, ver);
        assert(res1.second);
    }
}

void GraftletLoader::DependencyGraph::initialize(GraftletLoader& gl)
{
    std::vector<std::tuple<DllName,Version,Dependencies>> vec;
    vec.reserve(gl.m_name2lib.size());
    for(auto& item : gl.m_name2lib)
    {
        Dependencies& deps = std::get<3>(item.second); //Dependencies
        Version& ver = std::get<1>(item.second);
        vec.emplace_back(std::make_tuple(item.first, ver, deps));
    }
    initialize(vec);
}

//returns list to remove
std::vector<GraftletLoader::DependencyGraph::DllName> GraftletLoader::DependencyGraph::removeFailedDependants()
{
    std::vector<DllName> res;

    for(bool changed = true; changed; )
    {
        changed = false;
        for(auto it_gr = m_graph.begin(), it_gre = m_graph.end(); it_gr != it_gre;)
        {
            const DllName& name = it_gr->first;
            const list_t& list = it_gr->second;
            bool ok = true;
            //special case if the dependency format is violated; see initialize(...)
            if(list.size() == 1 && list.begin()->first.empty())
            {
                LOG_PRINT_L2("graftlet '") << name << "' has invalid dependency format. it will be unloaded.";
                ok = false;
            }
            else
            {
                for(auto& it1 : list)
                {
                    const DllName& dep_name = it1.first;
                    const Version& minver = it1.second;

                    auto it = m_graph.find(dep_name);
                    if(it == m_graph.end())
                    {
                        LOG_PRINT_L2("graftlet '") << name << "' depends on '" << dep_name << "' which is not found. it will be unloaded.";
                        ok = false;
                        break;
                    }
                    Version dep_ver = m_dll2ver[dep_name];
                    if(m_RemoveIfCmpMinverVer(dep_ver, minver))
                    {
                        LOG_PRINT_L2("graftlet '") << name << "' depends on '" << dep_name << "' which verson "
                                                   << GRAFTLET_Major(dep_ver) << "." << GRAFTLET_Minor(dep_ver) << " is less than required "
                                                   << GRAFTLET_Major(minver) << "." << GRAFTLET_Minor(minver) << ". it will be unloaded.";
                        ok = false;
                        break;
                    }
                    if(!ok) break;
                }
            }
            auto it0 = it_gr;
            ++it_gr;
            if(!ok)
            {
                res.push_back(name);
                m_graph.erase(it0);
                m_dll2ver.erase(name);
                changed = true;
                break;
            }
        }
    }
    return res;
}

void GraftletLoader::DependencyGraph::removeFailedDependants(GraftletLoader& gl)
{
    std::vector<DllName> list = removeFailedDependants();
    for(auto& dllName : list)
    {
        gl.m_name2lib.erase(dllName);
        gl.m_name2registries.erase(dllName);
    }
    assert(gl.m_name2lib.size() == gl.m_name2registries.size());
}

std::string GraftletLoader::DependencyGraph::findCycles(bool dont_throw)
{
    //find graph cycles
    // (graph_t::iterator is a map::iterator, the iterators are not bidirectional and have no compare method)
    auto cmp_graph_iters = [](const graph_t::iterator l, const graph_t::iterator r)->bool { return l->first < r->first; };
    using vert_clr_t = std::map<graph_t::iterator, Color, decltype(cmp_graph_iters)>;
    vert_clr_t vert_clr(cmp_graph_iters);
    //init with white
    for(auto it = m_graph.begin(), eit = m_graph.end(); it!=eit; ++it)
    {
        auto res = vert_clr.try_emplace(it, Color::white);
        assert(res.second);
    }

    auto color = [](vert_clr_t::iterator cur_it)->Color& { return cur_it->second; };

    auto find_child = [this, &vert_clr] (DllName& child) -> vert_clr_t::iterator
    {
        auto child_iter = m_graph.find(child);
        assert(child_iter != m_graph.end());
        auto it = vert_clr.find(child_iter);
        assert(it != vert_clr.end());
        return it;
    };

    while(true)
    {
        auto cur_it = vert_clr.begin();
        //find first white color
        for(; (cur_it != vert_clr.end()) && color(cur_it) != Color::white; ++cur_it );
        if(cur_it == vert_clr.end()) break; //not found; really all done

        //almost a stack
        std::vector< vert_clr_t::iterator > path;
        color(cur_it) = Color::gray;
        //depth first search
        for(;;)
        {
            list_t& list = cur_it->first->second;
            //find white child
            bool found = false;
            for(auto& item : list)
            {
                DllName& child = item.first;
                auto child_iter = find_child(child);
                if(color(child_iter) == Color::gray)
                {//cycle found
                    //find the child in the path
                    auto start = std::find(path.begin(), path.end(), child_iter);
                    assert(start != path.end() || child_iter == cur_it); //or depends on itself
                    std::ostringstream oss;
                    oss << "graftlet dependency cycle found: ";
                    std::for_each(start, path.end(), [&oss](auto it){ oss << it->first->first << " -> "; } );
                    oss << cur_it->first->first << " -> " << child;
                    if(dont_throw) return oss.str();
                    throw graft::exit_error(oss.str());
                }
                if(color(child_iter) == Color::white)
                {
                    path.push_back(cur_it);
                    cur_it = child_iter;
                    color(cur_it) = Color::gray;
                    found = true;
                    break;
                }
                assert(color(child_iter) == Color::black);
            }
            if(!found)
            {//step back
                color(cur_it) = Color::black;
                if(path.empty()) break; //all done (but maybe a white unreachable from those that have been looked, still exists)
                cur_it = *path.rbegin();
                path.pop_back();
            }
        }
    }
    return std::string();
}

void GraftletLoader::checkDependencies()
{
    DependencyGraph graph;
    graph.initialize(*this);
    graph.removeFailedDependants(*this);
    graph.findCycles();
}

} //namespace graftlet
