/*
 * Copyright (c) 2014 Clark Cianfarini
 * Copyright (c) 2018 GraftNetwork
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

namespace graftlet
{

bool GraftletLoader::findGraftletsAtDirectory(std::string directory, std::string extension)
{
    namespace fs = boost::filesystem;
    namespace dll = boost::dll;

    for(fs::directory_iterator it(directory), eit; it != eit; ++it)
    {
        if(fs::is_directory(*it)) continue;

        if(it->path().extension() != "."+extension) continue;

        std::cout << "." + extension + " library found:'" << it->path().c_str() <<"'\n";

        bool loaded = false;
        try
        {
            dll::shared_library lib(it->path(), dll::load_mode::append_decorations);
            loaded = true;

            if(!lib.has("getBuildSignature"))
            {
                std::cout << "\t" "getBuildSignature" " not found\n";
                continue;
            }

            auto getGraftletABI = dll::import<decltype(getBuildSignature)>(lib, "getBuildSignature");
            std::string graftletABI = getGraftletABI();
            if(graftletABI != getBuildSignature())
            {
                std::cout << "\tgraftlet ABI does not match '" << graftletABI << "' != '" << getBuildSignature() << "'\n";
                continue;
            }

            if(!lib.has("getGraftletRegistry")) continue;

            auto graftletRegistryAddr = dll::import<RegFunc>(lib, "getGraftletRegistry" );
            std::cout << typeid(graftletRegistryAddr).name() << "\n";

            GraftletRegistry* graftletRegistry = graftletRegistryAddr();

            int graftletVersion = 0;
            try
            {
                auto graftletFileVersion = dll::import<VersionFunc>(lib, "getGraftletVersion");
                graftletVersion = graftletFileVersion();
            }
            catch(std::exception& ex)
            {

            }

            auto getGraftletNameFunc = dll::import<decltype(getGraftletName)>(lib, "getGraftletName");
            dll_name_t dll_name = getGraftletNameFunc();
            dll_path_t dll_path = it->path().c_str();
            auto res = m_name2lib.emplace(
                        std::make_pair(dll_name,
                                       std::make_tuple( std::move(lib), graftletVersion, std::move(dll_path) ))
                        );
            if(!res.second) throw std::runtime_error("A plugin with the name '" + dll_name + "' already exists");
            auto res1 = m_name2registries.emplace( std::make_pair(dll_name, graftletRegistry) );
            assert(res1.second);
        }
        catch(std::exception& ex)
        {
            if(!loaded)
            {
                std::cout << "cannot load library: '" << it->path() << "' because '" << ex.what() << "'\n";
                continue;
            }
            else throw;
        }
    }

    return true;
}

}
