/*
* Copyright (c) 2014 Clark Cianfarini
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

#pragma once

#include <map>
#include <vector>
#include <string>
#include <any>
#include <iostream>
#include <boost/dll/import.hpp>
#include "GraftletRegistry.h"
#include "router.h"

namespace graftlet
{

template <class BaseT>
class GraftletExport
{
    template<typename Sign> class helperS;

    template <typename Res, typename...Ts>
    class helperS<Res(Ts...)>
    {
    public:
        using res_t = Res;
        using sign_t = Res(Ts...);

        helperS(GraftletExport* ge) : ge(ge) { }

        template <typename...Args>
        Res invoke(const std::string& cls_method, Args&&...args)
        {
            return (Res)ge->invoke<Res,Ts...>(cls_method, std::forward<Args>(args)...);
        }
    private:
        GraftletExport* ge;
    };

public:
    using gl_name_t = std::string;

    GraftletExport(const std::map<gl_name_t, std::any>& gl2any) : m_gl2any(gl2any) { }

    template <typename Res, typename...Ts, typename = Res(Ts...), typename...Args>
    Res invoke(const std::string& cls_method, Args&&...args)
    {
        gl_name_t cls;
        std::string method;
        {
            int pos = cls_method.find('.');
            if(pos != std::string::npos)
            {
                cls = cls_method.substr(0, pos);
                method = cls_method.substr(pos+1);
            }
            else
            {
                cls = cls_method;
            }
        }
        auto it = m_gl2any.find(cls);
        if(it == m_gl2any.end()) throw std::runtime_error("Cannot find graftlet class name:" + cls);
        std::shared_ptr<BaseT> concreteGraftlet = std::any_cast<std::shared_ptr<BaseT>>(it->second);
        return (Res)concreteGraftlet->template invoke<Res,Ts...>(method, std::forward<Args>(args)...);
    }

    template <typename Sign, typename...Args, typename Res = typename helperS<Sign>::res_t>
    Res invokeS(const std::string& cls_method, Args&&...args)
    {
        struct helperS<Sign> h(this);
        return h.invoke(cls_method, std::forward<Args>(args)...);
    }
private:
    const std::map<gl_name_t, std::any>& m_gl2any;
};

class GraftletLoader
{
private:
    using dll_name_t = std::string;
    using gl_name_t = std::string;
    using dll_path_t = std::string;
    using endpoint_t = std::string;
    using version_t = int;

    //we can use functions in a dll until we release object of boost::dll::shared_library
    //dll name -> (lib, version, path)
    std::map<dll_name_t, std::tuple<boost::dll::shared_library, version_t, dll_path_t>> m_name2lib;
    //dll name -> registry
    std::map<dll_name_t, GraftletRegistry*> m_name2registries;
    //dll (name, type_index of BaseT) -> (graftlet name, any of BaseT)
    //it makes no sense to hold std::shared_ptr<IGraftlet> until the shared_ptr is returned from resolveGraftlet
    std::map< std::pair<dll_name_t, std::type_index>, std::map<gl_name_t, std::any> > m_name2gls;

    template <class BaseT>
    void prepareAllEndpoints()
    {
        for(auto& item : m_name2lib)
        {
            buildAndResolveGraftletX<BaseT>(item.first);
        }
    }

    //graftlets exception list
    static bool is_in_GEL(dll_name_t name, int ver)
    {
        auto it = exception_list.find(name);
        if(it == exception_list.end()) return false;
        for(auto& rng : it->second)
        {
            int m = rng.first, M = rng.second;
            if(m <= ver && ver <= M) return true;
        }
        return false;
    }

public:
    using exception_list_vec_t = std::vector<std::pair<int,int>>;
    using exception_list_t = std::map<dll_name_t, exception_list_vec_t>;

    static exception_list_t exception_list;
    static int version;

    template <class BaseT>
    typename BaseT::endpoints_vec_t getEndpoints()
    {
        prepareAllEndpoints<BaseT>();

        typename BaseT::endpoints_vec_t res;
        for(auto& it0 : m_name2gls)
        {
            if(it0.first.second != std::type_index(typeid(BaseT))) continue;
            std::map<gl_name_t, std::any>& map = it0.second;
            for(auto& it1 : map)
            {
                //TODO: remove shared_ptr, it does not hold something now
                std::shared_ptr<BaseT> concreteGraftlet = std::any_cast<std::shared_ptr<BaseT>>(it1.second);
                typename BaseT::endpoints_vec_t vec = concreteGraftlet->getEndpoints();
                res.insert(res.end(), vec.begin(), vec.end());
            }
        }
        return res;
    }


    template <class BaseT>
    GraftletExport<BaseT> buildAndResolveGraftletX(const dll_name_t& dll_name)
    {
        auto it0 = m_name2gls.find(std::make_pair(dll_name,std::type_index(typeid(BaseT))));
        if(it0 == m_name2gls.end())
        {//find in dlls and insert it
            auto it1 = m_name2registries.find(dll_name);
            if(it1 == m_name2registries.end()) throw std::runtime_error("Cannot find dll_name:" + dll_name);
            GraftletRegistry* gr = it1->second;
            std::shared_ptr<BaseT> concreteGraftlet = gr->resolveGraftlet<BaseT>();
            if(!concreteGraftlet.get()) throw std::runtime_error("Cannot resolve dll_name:" + dll_name + " type:" + typeid(BaseT).name());
            concreteGraftlet->init();
            gl_name_t name = concreteGraftlet->getName();
            std::any any(concreteGraftlet);

            std::map<gl_name_t, std::any> map;
            map.insert(std::make_pair( std::move(name), std::move(any) ));

            auto res = m_name2gls.emplace(std::make_pair( std::make_pair(dll_name,std::type_index(typeid(BaseT))), std::move(map) ));
            assert(res.second);
            it0 = res.first;
        }

        std::map<gl_name_t, std::any>& map = it0->second;

        return GraftletExport<BaseT>(map);
    }

    //it is not used now
    template <class BaseT>
    std::vector<std::shared_ptr<BaseT>> buildAndResolveGraftlet(const std::string& path)
    {
        dll_name_t dll_name;
        gl_name_t gl_name;
        {
            int pos = path.find('.');
            if(pos != std::string::npos)
            {
                dll_name = path.substr(0, pos);
                gl_name = path.substr(pos+1);
            }
            else
            {
                dll_name = path;
            }
        }

        auto it0 = m_name2gls.find(std::make_pair(dll_name,std::type_index(typeid(BaseT))));
        if(it0 == m_name2gls.end())
        {
            auto it1 = m_name2registries.find(dll_name);
            if(it1 == m_name2registries.end()) throw std::runtime_error("Cannot find dll_name:" + dll_name);
            GraftletRegistry* gr = it1->second;
            std::shared_ptr<BaseT> concreteGraftlet = gr->resolveGraftlet<BaseT>();
            if(!concreteGraftlet.get()) throw std::runtime_error("Cannot resolve dll_name:" + dll_name + " type:" + typeid(BaseT).name());
            concreteGraftlet->init();
            gl_name_t name = concreteGraftlet->getName();
            std::any any(concreteGraftlet);

            std::map<gl_name_t, std::any> map;
            map.insert(std::make_pair( std::move(name), std::move(any) ));

            auto res = m_name2gls.emplace(std::make_pair( std::make_pair(dll_name,std::type_index(typeid(BaseT))), std::move(map) ));
            assert(res.second);
            it0 = res.first;
        }

        std::map<gl_name_t, std::any>& map = it0->second;
        auto it2 = map.find(gl_name);
        if(it2 == map.end())  throw std::runtime_error("Cannot find name '" + gl_name + "' in  dll_name:" + dll_name + " type:" + typeid(BaseT).name());
        std::shared_ptr<BaseT> concreteGraftlet = std::any_cast<std::shared_ptr<BaseT>>(it2->second);

        std::vector<std::shared_ptr<BaseT>> result {concreteGraftlet};
        return result;
    }

    bool findGraftletsAtDirectory(std::string additionalDir, std::string extension);

};
}

