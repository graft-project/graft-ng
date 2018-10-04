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
#include "GraftletLoader.h"
#include <misc_log_ex.h>

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

void GraftletLoader::findGraftletsAtDirectory(std::string directory, std::string extension)
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
            CHECK("getGraftletDependencies");
            CHECK("getGraftletRegistry");
#undef CHECK
/*
            if(!lib.has("getBuildSignature"))
            {
                LOG_PRINT_L2("getBuildSignature") "not found '" << it->path().c_str() << "'";
                continue;
            }
*/
            auto getGraftletABI = dll::import<decltype(getBuildSignature)>(lib, "getBuildSignature");
            std::string graftletABI = getGraftletABI();
            if(graftletABI != getBuildSignature())
            {
                LOG_PRINT_L2("\tgraftlet ABI does not match '") << graftletABI << "' != '" << getBuildSignature() << "'";
                continue;
            }

//            if(!lib.has("getGraftletRegistry")) continue;

            auto graftletRegistryAddr = dll::import<decltype(getGraftletRegistry)>(lib, "getGraftletRegistry" );
            GraftletRegistry* graftletRegistry = graftletRegistryAddr();

            auto graftletFileVersion = dll::import<decltype(getGraftletVersion)>(lib, "getGraftletVersion");
            int graftletVersion = graftletFileVersion();

            auto getGraftletNameFunc = dll::import<decltype(getGraftletName)>(lib, "getGraftletName");
            DllName dllName = getGraftletNameFunc();

            auto getGraftletDependenciesFunc = dll::import<decltype(getGraftletDependencies)>(lib, "getGraftletDependencies");
            Dependencies dependencies = getGraftletDependenciesFunc();

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
                LOG_PRINT_L2("cannot load library: '" << it->path() << "' because '" << ex.what() << "'");
                continue;
            }
            else if(!called)
            {
                LOG_PRINT_L2("exception on calls of mandatory functions '" << it->path() << "' because '" << ex.what() << "'");
                continue;
            }
            else throw;
        }
    }
}

void GraftletLoader::checkDependencies()
{
    using list_t = std::list<std::pair<DllName,Version>>;
    //vertex -> edges
    using graph_t = std::map<DllName, list_t>;
    graph_t graph; //directed graph

    //initialize graph
    for(auto& item : m_name2lib)
    {
        std::string_view deps = std::get<3>(item.second); //Dependencies
        std::list<std::pair<DllName,Version>> list;
        for(std::string::size_type s = 0;;)
        {
            std::string::size_type e = deps.find(',',s);
            std::string_view nv = (e == std::string::npos)? deps.substr(s) : deps.substr(s,e-s);

            std::string_view name;
            Version minver;

            std::string::size_type pos = nv.find(':',s);
            if(pos != std::string::npos)
            {
                name = nv.substr(0,pos);
                std::istringstream iss(std::string( nv.substr(pos+1) ));
                iss >> minver;
                if(iss.fail()) minver = std::numeric_limits<Version>::max();
            }
            else
            {
                name = nv;
                minver = 0;
            }

            list.push_back(std::make_pair(DllName(name),minver));

            if(e == std::string::npos) break;
            s = e + 1;
        }
        auto res = graph.emplace(item.first, std::move(list));
        assert(res.second);
    }

    //check dependencies
    for(bool changed = true; changed; changed = false)
    {
        for(auto it_gr = graph.begin(), it_gre = graph.end(); it_gr != it_gre;)
        {
            const DllName& name = it_gr->first;
            const list_t& list = it_gr->second;
            bool ok = true;
            for(auto& it1 : list)
            {
                const DllName& dep_name = it1.first;
                const Version& minver = it1.second;
                auto it = m_name2lib.find(dep_name);
                if(it == m_name2lib.end())
                {
                    LOG_PRINT_L2("graftlet '") << name << "' depends on '" << dep_name << "' which is not found. it will be unloaded.";
                    ok = false;
                    break;
                }
                Version dep_ver = std::get<1>(it->second);
                if(dep_ver < minver)
                {
                    LOG_PRINT_L2("graftlet '") << name << "' depends on '" << dep_name << "' which verson "
                                               << GRAFTLET_Major(dep_ver) << "." << GRAFTLET_Minor(dep_ver) << " is less than required "
                                               << GRAFTLET_Major(minver) << "." << GRAFTLET_Minor(minver) << ". it will be unloaded.";
                    ok = false;
                    break;
                }
                if(!ok) break;
            }
            auto it0 = it_gr;
            ++it_gr;
            if(!ok)
            {
                m_name2lib.erase(name);
                m_name2registries.erase(name);
                graph.erase(it0);
                changed;
            }
        }
    }

    //find graph cycles
    using color_t = int; //0 - white, 1 - gray, 2 - black
    // (graph_t::iterator is a map::iterator, the iterators are not bidirectional and have no compare method)
    auto cmp_graph_iters = [](const graph_t::iterator l, const graph_t::iterator r)->bool { return l->first < r->first; };
    using vert_clr_t = std::map<graph_t::iterator, color_t, decltype(cmp_graph_iters)>;
    vert_clr_t vert_clr(cmp_graph_iters);
    //init with white
    for(auto it = graph.begin(), eit = graph.end(); it!=eit; ++it)
    {
        auto res = vert_clr.try_emplace(it,0);
        assert(res.second);
    }

    auto find_child = [&graph, &vert_clr] (DllName& child) -> vert_clr_t::iterator
    {
        auto child_iter = graph.find(child);
        assert(child_iter != graph.end());
        auto it = vert_clr.find(child_iter);
        assert(it != vert_clr.end());
        return it;
    };

    while(true)
    {
        auto cur_it = vert_clr.begin();
        //find first white color
        for(; (cur_it != vert_clr.end()) && (cur_it->second) != 0; ++cur_it );
        if(cur_it == vert_clr.end()) break; //not found; really all done

        //almost a stack
        std::vector< vert_clr_t::iterator > path;
        cur_it->second = 1; //gray
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
                if(child_iter->second == 1) //gray
                {//cycle found
                    //find the child in the path
                    auto start = std::find(path.begin(), path.end(), child_iter);
                    assert(start != path.end() || child_iter == cur_it); //or depends on itself
                    std::ostringstream oss;
                    oss << "graftlet dependency cycle found: ";
                    std::for_each(start, path.end(), [&oss](auto it){ oss << it->first->first << " -> "; } );
                    oss << cur_it->first->first << " -> " << child;
                    throw std::runtime_error(oss.str());
                }
                if(child_iter->second == 0) //white
                {
//                    child_iter->second = 1; //gray
                    path.push_back(cur_it);
                    cur_it = child_iter;
                    child_iter->second = 1; //gray
                    found = true;
                    break;
                }
                assert(child_iter->second == 2); //black
            }
            if(!found)
            {//step back
                cur_it->second = 2; //black
                if(path.empty()) break; //all done (but maybe a white unreachable from those that have been looked, still exists)
                cur_it = *path.rbegin();
                path.pop_back();
            }
        }
    }
}

}
