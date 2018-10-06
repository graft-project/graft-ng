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

bool GraftletLoader::findGraftletsAtDirectory(std::string directory, std::string extension)
{
    namespace fs = boost::filesystem;
    namespace dll = boost::dll;

    for(fs::directory_iterator it(directory), eit; it != eit; ++it)
    {
        if(fs::is_directory(*it)) continue;

        if(it->path().extension() != "."+extension) continue;

        LOG_PRINT_L2("." << extension << " library found:'" << it->path().c_str() << "'");

        bool loaded = false;
        try
        {
            dll::shared_library lib(it->path(), dll::load_mode::append_decorations);
            loaded = true;

            if(!lib.has("getBuildSignature"))
            {
                LOG_PRINT_L2("\t" "getBuildSignature" " not found");
                continue;
            }

            auto getGraftletABI = dll::import<decltype(getBuildSignature)>(lib, "getBuildSignature");
            std::string graftletABI = getGraftletABI();
            if(graftletABI != getBuildSignature())
            {
                LOG_PRINT_L2("\tgraftlet ABI does not match '") << graftletABI << "' != '" << getBuildSignature() << "'";
                continue;
            }

            if(!lib.has("getGraftletRegistry")) continue;

            auto graftletRegistryAddr = dll::import<decltype(getGraftletRegistry)>(lib, "getGraftletRegistry" );

            GraftletRegistry* graftletRegistry = graftletRegistryAddr();

            int graftletVersion = 0;
            try
            {
                auto graftletFileVersion = dll::import<decltype(getGraftletVersion)>(lib, "getGraftletVersion");
                graftletVersion = graftletFileVersion();
            }
            catch(std::exception& ex)
            {

            }

            auto getGraftletNameFunc = dll::import<decltype(getGraftletName)>(lib, "getGraftletName");
            DllName dllName = getGraftletNameFunc();
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
                                       std::make_tuple( std::move(lib), graftletVersion, std::move(dll_path) ))
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
            else throw;
        }
    }

    return true;
}

}
