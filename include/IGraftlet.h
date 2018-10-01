#pragma once

#include <map>
#include <string>
#include <functional>
#include <typeindex>
#include <typeinfo>
#include <type_traits>
#include <any>
#include <iostream>
#include <cassert>
#include <misc_log_ex.h>
#include "router.h"

#define REGISTER_ACTION(T, f) \
    register_handler_memf(#f, this, &T::f)

#define REGISTER_ENDPOINT(Endpoint, Methods, T, f) \
    register_endpoint_memf(#f, this, &T::f, Endpoint, Methods)

class IGraftlet
{
public:
    using ClsName = std::string;
    using FuncName = std::string;
    using EndpointPath = std::string;
    using Methods = int;
    using EndpointsVec = std::vector< std::tuple<EndpointPath, Methods, graft::Router::Handler> >;

    IGraftlet() = delete;
    virtual ~IGraftlet() = default;
    IGraftlet(const IGraftlet&) = delete;
    IGraftlet& operator = (const IGraftlet&) = delete;

    void init()
    {
        if(m_inited) return;
        m_inited = true;
        initOnce();
    }

    const ClsName& getClsName() const { return m_clsName; }

    EndpointsVec getEndpoints()
    {
        EndpointsVec res;
        std::type_index ti = std::type_index(typeid(graft::Router::Handler));
        for(auto& it : m_map)
        {
            TypeIndex2any& ti2any = it.second;
            auto it1 = ti2any.find(ti);
            if(it1 == ti2any.end()) continue;

            std::any& any = std::get<0>(it1->second);
            EndpointPath& endpoint = std::get<1>(it1->second);
            Methods& methods = std::get<2>(it1->second);

            graft::Router::Handler handler = std::any_cast<graft::Router::Handler>(any);

            res.emplace_back(std::make_tuple(endpoint, methods, handler));
        }
        return res;
    }

    template <typename Res, typename...Ts, typename = Res(Ts...), typename...Args>
    Res invoke(const FuncName& name, Args&&...args)
    {
        using Callable = std::function<Res (Ts...)>;
        std::type_index ti = std::type_index(typeid(Callable));

        auto it = m_map.find(name);
        if(it == m_map.end())  throw std::runtime_error("cannot find function " + name);
        TypeIndex2any& ti2any = it->second;
        auto it1 = ti2any.find(ti);
        if(it1 == ti2any.end()) throw std::runtime_error("cannot find function " + name + " with typeid " + ti.name() );

        std::any& any = std::get<0>(it1->second);

        Callable callable = std::any_cast<Callable>(any);

        return callable(std::forward<Args>(args)...);
    }

    //It can be used to register any callable object like a function, to register member function use register_handler_memf
    template<typename Res,  typename...Ts, typename Callable = Res (Ts...)>
    void register_handler(const FuncName& name, Callable callable, const EndpointPath& endpoint = EndpointPath(), Methods methods = 0)
    {
        std::type_index ti = std::type_index(typeid(Callable));
        TypeIndex2any& ti2any = m_map[name];
        std::any any = std::make_any<Callable>(callable);
        assert(any.type().hash_code() == typeid(callable).hash_code());

        std::ostringstream oss;
        if(!endpoint.empty())
        {
            oss << " '" << endpoint << "' " << graft::Router::methodsToString(methods);
        }
        LOG_PRINT_L2("register_handler " << name << oss.str() << " of " << typeid(callable).name());
        auto res = ti2any.emplace(ti, std::make_tuple(std::move(any), endpoint, methods) );
        if(!res.second) throw std::runtime_error("function " + name + " with typeid " + ti.name() + " already registered");
    }

    template<typename Obj, typename Res,  typename...Ts>
    void register_handler_memf(const FuncName& name, Obj* p, Res (Obj::*f)(Ts...))
    {
        std::function<Res(Obj*,Ts...)> memf = f;
        std::function<Res(Ts...)> fun = [p,memf](Ts&&...ts)->Res { return memf(p,std::forward<Ts>(ts)...); };
        register_handler<Res, Ts...,decltype(fun)>(name, fun);
    }

    //It can be used to register any Handler like a function, to register member function use register_endpoint_memf
    void register_endpoint(const FuncName& name, graft::Router::Handler worker_action, const EndpointPath& endpoint, Methods methods)
    {
        register_handler<graft::Status>(name, worker_action, endpoint, methods);
    }

    template<typename Obj>
    void register_endpoint_memf(const FuncName& name, Obj* p
                                , graft::Status (Obj::*f)(const graft::Router::vars_t& vars, const graft::Input& input, graft::Context& ctx, graft::Output& output)
                                , const EndpointPath& endpoint, Methods methods )
    {
        std::function<graft::Status (Obj*,const graft::Router::vars_t& vars, const graft::Input& input, graft::Context& ctx, graft::Output& output)> memf = f;
        graft::Router::Handler fun =
                [p,memf](const graft::Router::vars_t& vars, const graft::Input& input, graft::Context& ctx, graft::Output& output)->graft::Status
        {
            return memf(p,vars,input,ctx,output);
        };
        register_endpoint(name, fun, endpoint, methods);
    }
protected:
    IGraftlet(const ClsName& name = ClsName() ) : m_clsName(name) { }
    virtual void initOnce() = 0;
private:
    using TypeIndex2any = std::map<std::type_index, std::tuple<std::any, EndpointPath, Methods> >;
    using Map = std::map<FuncName, TypeIndex2any>;

    bool m_inited = false;
    ClsName m_clsName;
    Map m_map;
};

