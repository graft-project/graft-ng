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
    register_endpoint_memf(#f, this, &T::f, Endpoint, Methods, false)

#define REGISTER_GENERIC(Endpoint, Methods, T, f) \
    register_endpoint_memf(#f, this, &T::f, Endpoint, Methods, true)

class IGraftlet
{
public:
    using handler_tag_t = std::string;
    using func_name_t = std::string;
    using endpoint_t = std::string;
    using methods_t = int;
    using is_call_t = bool; //is the function intended to be called directly (endpoint case)
    using ti2any_t = std::map<std::type_index, std::tuple<std::any, endpoint_t, methods_t, is_call_t> >;
    using map_t = std::map<func_name_t, ti2any_t>;
    using EndpointsVec = std::vector< std::tuple<endpoint_t, methods_t, graft::Router::Handler> >;

    void init()
    {
        if(inited) return;
        inited = true;
        initOnce();
    }

    IGraftlet() = delete;
    virtual ~IGraftlet() = default;
    IGraftlet(const IGraftlet&) = delete;
    IGraftlet& operator = (const IGraftlet&) = delete;

    const std::string& getName() const { return m_name; }

    EndpointsVec getEndpoints()
    {
        EndpointsVec res;
        std::type_index ti = std::type_index(typeid(graft::Router::Handler));
        for(auto& it : map)
        {
            ti2any_t& ti2any = it.second;
            auto it1 = ti2any.find(ti);
            if(it1 == ti2any.end()) continue;

            std::any& any = std::get<0>(it1->second);
            endpoint_t& endpoint = std::get<1>(it1->second);
            methods_t& methods = std::get<2>(it1->second);

            graft::Router::Handler handler = std::any_cast<graft::Router::Handler>(any);

            res.emplace_back(std::make_tuple(endpoint, methods, handler));
        }
        return res;
    }

    template <typename Res, typename...Ts, typename = Res(Ts...), typename...Args>
    Res invoke(const func_name_t& name, Args&&...args)
    {
        using Callable = std::function<Res (Ts...)>;
        std::type_index ti = std::type_index(typeid(Callable));

        auto it = map.find(name);
        if(it == map.end())  throw std::runtime_error("cannot find function " + name);
        ti2any_t& ti2any = it->second;
        auto it1 = ti2any.find(ti);
        if(it1 == ti2any.end()) throw std::runtime_error("cannot find function " + name + " with typeid " + ti.name() );

        std::any& any = std::get<0>(it1->second);
        is_call_t& is_call = std::get<3>(it1->second);
        if(!is_call) throw std::runtime_error("found function is no intended to be called, " + name + " with typeid " + ti.name() );

        Callable callable = std::any_cast<Callable>(any);

        return callable(std::forward<Args>(args)...);
    }

    template<typename Res,  typename...Ts, typename Callable = Res (Ts...)>
    void register_handler(const func_name_t& name, Callable callable, const endpoint_t& endpoint = endpoint_t(), methods_t methods = 0, is_call_t is_call = true)
    {
        std::type_index ti = std::type_index(typeid(Callable));
        ti2any_t& ti2any = map[name];
        std::any any = std::make_any<Callable>(callable);
        assert(any.type().hash_code() == typeid(callable).hash_code());

        std::ostringstream oss;
        if(!endpoint.empty())
        {
            oss << " '" << endpoint << "' " << graft::Router::methodsToString(methods) << " " << ((is_call)? "callable" : "non-callable") << " directly";
        }
        LOG_PRINT_L2("register_handler " << name << oss.str() << " of " << typeid(callable).name());
        auto res = ti2any.emplace(ti, std::make_tuple(std::move(any), endpoint, methods, is_call) );
        if(!res.second) throw std::runtime_error("function " + name + " with typeid " + ti.name() + " already registered");
    }

    template<typename Obj, typename Res,  typename...Ts>
    void register_handler_memf(const func_name_t& name, Obj* p, Res (Obj::*f)(Ts...))
    {
        std::function<Res(Obj*,Ts...)> memf = f;
        std::function<Res(Ts...)> fun = [p,memf](Ts&&...ts)->Res { return memf(p,std::forward<Ts>(ts)...); };
        register_handler<Res, Ts...,decltype(fun)>(name, fun);
    }

    void register_endpoint(const func_name_t& name, graft::Router::Handler worker_action, const endpoint_t& endpoint, methods_t methods, is_call_t is_call)
    {
        register_handler<graft::Status>(name, worker_action, endpoint, methods, is_call);
    }

    template<typename Obj>
    void register_endpoint_memf(const func_name_t& name, Obj* p
                                , graft::Status (Obj::*f)(const graft::Router::vars_t& vars, const graft::Input& input, graft::Context& ctx, graft::Output& output)
                                , const endpoint_t& endpoint, methods_t methods, is_call_t is_call )
    {
        std::function<graft::Status (Obj*,const graft::Router::vars_t& vars, const graft::Input& input, graft::Context& ctx, graft::Output& output)> memf = f;
        graft::Router::Handler fun =
                [p,memf](const graft::Router::vars_t& vars, const graft::Input& input, graft::Context& ctx, graft::Output& output)->graft::Status
        {
            return memf(p,vars,input,ctx,output);
        };
        register_handler<graft::Status>(name, fun, endpoint, methods, is_call);
    }
protected:
    IGraftlet(const std::string& name = std::string() ) : m_name(name) { }
    virtual void initOnce() = 0;
private:
    bool inited = false;
    std::string m_name;
    map_t map;
};

