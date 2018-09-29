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
#include "IGraftlet.h"
#include "GraftletRegistry.h"
#include "router.h"

namespace graftlet
{

template <class BaseT>
class GraftletHandlerT
{
private:
    //Helper class to resolve signature of a function
    template<typename Sign> class helperSign;

    template <typename Res, typename...Ts>
    class helperSign<Res(Ts...)>
    {
    public:
        using res_t = Res;
        using sign_t = Res(Ts...);

        helperSign(GraftletHandlerT* gh) : gh(gh) { }

        template <typename...Args>
        Res invoke(const std::string& cls_method, Args&&...args)
        {
            return (Res)gh->invokeRA<Res,Ts...>(cls_method, std::forward<Args>(args)...);
        }
    private:
        GraftletHandlerT* gh;
    };

    using ClsName_ = std::string; // the same as ClsName

    template <typename Res, typename...Ts, typename = Res(Ts...), typename...Args>
    Res invokeRA(const std::string& cls_method, Args&&...args)
    {
        ClsName_ cls;
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
        auto it = m_cls2any.find(cls);
        if(it == m_cls2any.end()) throw std::runtime_error("Cannot find graftlet class name:" + cls);
        std::shared_ptr<BaseT> concreteGraftlet = std::any_cast<std::shared_ptr<BaseT>>(it->second);
        return (Res)concreteGraftlet->template invoke<Res,Ts...>(method, std::forward<Args>(args)...);
    }

    const std::map<ClsName_, std::any>& m_cls2any;

public:
    using ClsName = std::string;

    GraftletHandlerT(const std::map<ClsName, std::any>& cls2any) : m_cls2any(cls2any) { }

    template <typename Sign, typename...Args, typename Res = typename helperSign<Sign>::res_t>
    Res invoke(const std::string& cls_method, Args&&...args)
    {
        struct helperSign<Sign> h(this);
        return h.invoke(cls_method, std::forward<Args>(args)...);
    }
};

class GraftletLoader
{
public:
    using DllName = std::string;
    using Version = int;
    using GraftletExceptionList = std::vector< std::pair< DllName, std::vector< std::pair<Version, Version> >>>;

    static Version getFwVersion() { return m_fwVersion; }
    static void setFwVersion(Version fwVersion) { m_fwVersion = fwVersion; }

    static void setGraftletsExceptionList(const GraftletExceptionList& gel);

    bool findGraftletsAtDirectory(std::string additionalDir, std::string extension);

    GraftletHandlerT<IGraftlet> buildAndResolveGraftlet(const DllName& dllName)
    {
        return buildAndResolveGraftletT<IGraftlet>(dllName);
    }

    typename IGraftlet::EndpointsVec getEndpoints()
    {
        return getEndpointsT<IGraftlet>();
    }

private:
    using ClsName = std::string;
    using DllPath = std::string;

    using ExceptionRngVec = std::vector<std::pair<Version,Version>>;
    using ExceptionMap = std::map<DllName, ExceptionRngVec>;

    //graftlets exception list
    static bool is_in_GEL(DllName name, Version ver)
    {
        auto it = m_exceptionMap.find(name);
        if(it == m_exceptionMap.end()) return false;
        for(auto& rng : it->second)
        {
            Version m = rng.first, M = rng.second;
            if(m <= ver && ver <= M) return true;
        }
        return false;
    }

    template <class BaseT>
    void prepareAllEndpoints()
    {
        for(auto& item : m_name2lib)
        {
            buildAndResolveGraftletT<BaseT>(item.first);
        }
    }

    template <class BaseT>
    typename BaseT::EndpointsVec getEndpointsT()
    {
        prepareAllEndpoints<BaseT>();

        typename BaseT::EndpointsVec res;
        for(auto& it0 : m_name2gls)
        {
            if(it0.first.second != std::type_index(typeid(BaseT))) continue;
            std::map<ClsName, std::any>& map = it0.second;
            for(auto& it1 : map)
            {
                //TODO: remove shared_ptr, it does not hold something now
                std::shared_ptr<BaseT> concreteGraftlet = std::any_cast<std::shared_ptr<BaseT>>(it1.second);
                typename BaseT::EndpointsVec vec = concreteGraftlet->getEndpoints();
                res.insert(res.end(), vec.begin(), vec.end());
            }
        }
        return res;
    }

    template <class BaseT>
    GraftletHandlerT<BaseT> buildAndResolveGraftletT(const DllName& dllName)
    {
        auto it0 = m_name2gls.find(std::make_pair(dllName,std::type_index(typeid(BaseT))));
        if(it0 == m_name2gls.end())
        {//find in dlls and insert it
            auto it1 = m_name2registries.find(dllName);
            if(it1 == m_name2registries.end()) throw std::runtime_error("Cannot find dll name:" + dllName);
            GraftletRegistry* gr = it1->second;
            std::shared_ptr<BaseT> concreteGraftlet = gr->resolveGraftlet<BaseT>();
            if(!concreteGraftlet.get()) throw std::runtime_error("Cannot resolve dll name:" + dllName + " type:" + typeid(BaseT).name());
            concreteGraftlet->init();
            ClsName name = concreteGraftlet->getName();
            std::any any(concreteGraftlet);

            std::map<ClsName, std::any> map;
            map.insert(std::make_pair( std::move(name), std::move(any) ));

            auto res = m_name2gls.emplace(std::make_pair( std::make_pair(dllName,std::type_index(typeid(BaseT))), std::move(map) ));
            assert(res.second);
            it0 = res.first;
        }

        std::map<ClsName, std::any>& map = it0->second;

        return GraftletHandlerT<BaseT>(map);
    }

#if(0)
    //It is not used now. It can find IGraftlets using "dll_name.cls_name"
    template <class BaseT>
    std::vector<std::shared_ptr<BaseT>> buildAndResolveGraftletDllClass(const std::string& path)
    {
        DllName dllName;
        ClsName clsName;
        {
            int pos = path.find('.');
            if(pos != std::string::npos)
            {
                dllName = path.substr(0, pos);
                clsName = path.substr(pos+1);
            }
            else
            {
                dllName = path;
            }
        }

        auto it0 = m_name2gls.find(std::make_pair(dllName,std::type_index(typeid(BaseT))));
        if(it0 == m_name2gls.end())
        {
            auto it1 = m_name2registries.find(dllName);
            if(it1 == m_name2registries.end()) throw std::runtime_error("Cannot find dll name:" + dllName);
            GraftletRegistry* gr = it1->second;
            std::shared_ptr<BaseT> concreteGraftlet = gr->resolveGraftlet<BaseT>();
            if(!concreteGraftlet.get()) throw std::runtime_error("Cannot resolve dll name:" + dllName + " type:" + typeid(BaseT).name());
            concreteGraftlet->init();
            ClsName name = concreteGraftlet->getName();
            std::any any(concreteGraftlet);

            std::map<ClsName, std::any> map;
            map.insert(std::make_pair( std::move(name), std::move(any) ));

            auto res = m_name2gls.emplace(std::make_pair( std::make_pair(dllName,std::type_index(typeid(BaseT))), std::move(map) ));
            assert(res.second);
            it0 = res.first;
        }

        std::map<ClsName, std::any>& map = it0->second;
        auto it2 = map.find(clsName);
        if(it2 == map.end())  throw std::runtime_error("Cannot find name '" + clsName + "' in  dll name:" + dllName + " type:" + typeid(BaseT).name());
        std::shared_ptr<BaseT> concreteGraftlet = std::any_cast<std::shared_ptr<BaseT>>(it2->second);

        std::vector<std::shared_ptr<BaseT>> result {concreteGraftlet};
        return result;
    }
#endif


    static Version m_fwVersion;
    static ExceptionMap m_exceptionMap;

    //we can use functions in a dll until we release object of boost::dll::shared_library
    //dll name -> (lib, version, path)
    std::map<DllName, std::tuple<boost::dll::shared_library, Version, DllPath>> m_name2lib;
    //dll name -> registry
    std::map<DllName, GraftletRegistry*> m_name2registries;
    //dll (name, type_index of BaseT) -> (class name, any of BaseT)
    //it makes no sense to hold std::shared_ptr<IGraftlet> until the shared_ptr is returned from resolveGraftlet
    std::map< std::pair<DllName, std::type_index>, std::map<ClsName, std::any> > m_name2gls;

};

using GraftletHandler = GraftletHandlerT<IGraftlet>;

} //namespace graftlet

