#pragma once

#include <map>
#include <string>
#include <functional>
#include <typeindex>
#include <typeinfo>
#include <type_traits>
#include <any>

#include <iostream>
#include <functional>

#include <cassert>
#include "router.h"

#define REGISTER_ENDPOINT(Endpoint,T, f) \
    register_endpoint_memf(Endpoint, this, &T::f)

#define REGISTER_ACTION(T, f) \
    register_handler_memf(#f, this, &T::f)

class IGraftlet
{
public:
    using handler_tag_t = std::string;

    virtual void init() = 0;

    IGraftlet(const std::string& name = std::string() ) : m_name(name) { }
    virtual ~IGraftlet() = default;
    IGraftlet(const IGraftlet&) = delete;
    IGraftlet& operator = (const IGraftlet&) = delete;

    const std::string& getName() const { return m_name; }
public:
    using method_name_t = std::string;
    using ti2any_t = std::map<std::type_index, std::any>;
    using map_t = std::map<method_name_t, ti2any_t>;
    map_t map;

    template<typename Obj, typename Res,  typename...Ts>
    class memf_t
    {
        Obj* p;
        Res (Obj::*f)(Ts...);
    public:
        memf_t(Obj* p, Res (Obj::*f)(Ts...)) : p(p), f(f) { }
        template<typename...Args>
        Res operator ()(Args&&...args)
        {
            return (p->*f)(std::forward<Args>(args)...);
        }
    };

    template <typename Res, typename...Ts, typename Sign = Res(Ts...), typename...Args>
    Res invokeX(const method_name_t& name, Args&&...args)
    {
        using Callable = std::function<Res (Ts...)>;
        std::type_index ti = std::type_index(typeid(Callable));

        auto it = map.find(name);
        if(it == map.end())  throw std::runtime_error("cannot find method " + name);
        ti2any_t& ti2any = it->second;
        auto it1 = ti2any.find(ti);
        if(it1 == ti2any.end()) throw std::runtime_error("cannot find method " + name + " with typeid " + ti.name() );

        std::any& any = it1->second;
        Callable callable = std::any_cast<Callable>(any);

        return callable(std::forward<Args>(args)...);
    }

    template<typename Res,  typename...Ts, typename Callable = Res (Ts...)>
    void register_handler(const method_name_t& name, Callable callable)
    {
        std::type_index ti = std::type_index(typeid(Callable));
        ti2any_t& ti2any = map[name];
        std::any any = std::make_any<Callable>(callable);
        assert(any.type().hash_code() == typeid(callable).hash_code());
        std::cout << "register_handler " << name << " of " << typeid(callable).name() << "\n";
        auto res = ti2any.emplace(ti, std::move(any));
        if(!res.second) throw std::runtime_error("method " + name + " with typeid " + ti.name() + " already registered");
    }

    template<typename Obj, typename Res,  typename...Ts>
    void register_handler_memf(const method_name_t& name, Obj* p, Res (Obj::*f)(Ts...))
    {
        std::function<Res(Obj*,Ts...)> memf = f;
        std::function<Res(Ts...)> fun = [p,memf](Ts&&...ts)->Res { return memf(p,std::forward<Ts>(ts)...); };
        register_handler<Res, Ts...,decltype(fun)>(name, fun);
    }

    void register_endpoint(const std::string& endpoint, graft::Router::Handler worker_action)
    {
        register_handler<graft::Status>(endpoint, worker_action);
    }

    template<typename Obj>
    void register_endpoint_memf(const std::string& endpoint, Obj* p,
                                graft::Status (Obj::*f)(const graft::Router::vars_t& vars, const graft::Input& input, graft::Context& ctx, graft::Output& output))
    {
        std::function<graft::Status (Obj*,const graft::Router::vars_t& vars, const graft::Input& input, graft::Context& ctx, graft::Output& output)> memf = f;
        std::function<graft::Status (const graft::Router::vars_t& vars, const graft::Input& input, graft::Context& ctx, graft::Output& output)> fun =
                [p,memf](const graft::Router::vars_t& vars, const graft::Input& input, graft::Context& ctx, graft::Output& output)->graft::Status
        {
            return memf(p,vars,input,ctx,output);
        };
        register_handler<graft::Status>(endpoint, fun);
    }
private:
    std::string m_name;
};

