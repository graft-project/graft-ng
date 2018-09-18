#pragma once

#include <map>
#include <string>
#include <functional>
#include <tuple>
#include <typeindex>
#include <typeinfo>
#include <type_traits>
#include <utility>
#include <any>

#include <iostream>
#include <functional>

#include <cassert>

#define REGISTER_ACTIONX1(T, f) \
    register_handler_memf1(#f, this, &T::f)

#define REGISTER_ACTIONX(T, f) \
    register_handler_memf(#f, this, &T::f)

#define REGISTER_ACTION(T, f) \
    register_handler(#f, this, &T::f)

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

    template <typename...Ts,typename...Args>
    int invoke(const handler_tag_t& tag, Args&&...args)
    {
        auto any = args2any<Ts...>(std::forward<Args>(args)...);

        std::cout << "invoke:   " << any.type().name() << "\n";
        {
//            std::tuple<int,int> tu(1,2);
            std::string s = "sss";
            std::tuple<std::string&> tu(s);
            std::any any = tu;
            std::cout << "invoke: x " << any.type().name() << "\n";
        }

        auto it1 = m_members.find(any.type());
        if (it1 == m_members.end())
            return false;


        auto it2 = it1->second.find(tag);
        if (it2 == it1->second.end())
            return false;

        it2->second(any);
//        it2->second(args...);

        return true;
    }
/*
    template <typename... Args>
//    int invoke(const handler_tag_t& tag, Args&&... args)
    int invokeX(const handler_tag_t& tag, Args... args)
    {
        auto any = args2any<Args...>(args...);

        std::cout << "invoke:   " << any.type().name() << "\n";
        {
//            std::tuple<int,int> tu(1,2);
            std::string s = "sss";
            std::tuple<std::string&> tu(s);
            std::any any = tu;
            std::cout << "invoke: x " << any.type().name() << "\n";
        }

        auto it1 = m_members.find(any.type());
        if (it1 == m_members.end())
            return false;


        auto it2 = it1->second.find(tag);
        if (it2 == it1->second.end())
            return false;

        it2->second(any);
//        it2->second(args...);

        return true;
    }
*/
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
//        using Callable = Res (Ts...);
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
/*
    template<typename Obj, typename Res,  typename...Ts, typename...Args>
    Res X(Obj* p, void (Obj::*f)(Ts...), Args&&...args)
    {
        return (p->*f)(std::forward<Args>(args)...);
    }
*/
    template<typename Res,  typename...Ts, typename Callable = Res (Ts...)>
    void register_handlerX(const method_name_t& name, Callable callable)
    {
        std::type_index ti = std::type_index(typeid(Callable));
        ti2any_t& ti2any = map[name];
//        auto res = ti2any.emplace(ti, std::make_any<Callable>(callable));
        std::any any = std::make_any<Callable>(callable);
        assert(any.type().hash_code() == typeid(callable).hash_code());
//        auto res = ti2any.emplace(ti, std::make_any<Callable>(callable));
        std::cout << "register_handlerX " << name << " of " << typeid(callable).name() << "\n";
        auto res = ti2any.emplace(ti, std::move(any));
        if(!res.second) throw std::runtime_error("method " + name + " with typeid " + ti.name() + " already registered");
    }

    template<typename Obj, typename Res,  typename...Ts>
    void register_handler_memf(const method_name_t& name, Obj* p, Res (Obj::*f)(Ts...))
    {
//        std::function<Res(Obj*,Ts...)> fun = f;
        using std::placeholders::_1;
        std::function<Res(Ts...)> fun = std::bind( f, p, _1 );
        register_handlerX<Res, Ts...,decltype(fun)>(name, fun);
/*
        using memf_ty = memf_t<Obj,Res,Ts...>;
        memf_ty memf{p,f};
        register_handlerX<Res, Ts..., memf_ty>(name, memf);
*/
/*
        auto fun = [this, p, f](Ts&& ts...)->Res
        {
            return (p->*f)(std::forward<Ts> ts...)
            std::cout << "fun:" << params.type().name() << "\n";
        };
*/
    }

    template<typename Obj, typename Res,  typename...Ts>
    void register_handler_memf1(const method_name_t& name, Obj* p, Res (Obj::*f)(Ts...))
    {
        std::function<Res(Obj*,Ts...)> memf = f;
        std::function<Res(Ts...)> fun = [p,memf](Ts&&...ts)->Res { return memf(p,std::forward<Ts>(ts)...); };
        register_handlerX<Res, Ts...,decltype(fun)>(name, fun);
    }

    template<typename Obj, typename Res, typename...Ts>
    void register_handler_memf1(const method_name_t& name, Obj* p, std::function<Res(Obj*,Ts...)> f)
    {
//        std::function<Res(Obj*,Ts...)> fm = f;
//        std::function<Res(Ts...)> fun = [p,f](Ts...ts)->Res { return (p->*f)(ts...); };
        std::function<Res(Ts...)> fun = [p,f](Ts...ts)->Res { return f(p,ts...); };
        register_handlerX<Res, Ts...,decltype(fun)>(name, fun);
    }
/*
    template <typename Obj, typename Res, typename...Ts, typename Sign = Res(Ts...), typename...Args>
    Res invoke_memf(const method_name_t& name, Args&&...args)
    {
        using memf_ty = memf_t<Obj,Res,Ts...>;
//        using Callable = Res (Ts...);
        std::type_index ti = std::type_index(typeid(memf_ty));

        auto it = map.find(name);
        if(it == map.end())  throw std::runtime_error("cannot find method " + name);
        ti2any_t& ti2any = it->second;
        auto it1 = ti2any.find(ti);
        if(it1 == ti2any.end()) throw std::runtime_error("cannot find method " + name + " with typeid " + ti.name() );

        std::any& any = it1->second;
        memf_ty callable = std::any_cast<memf_ty>(any);

        return callable(std::forward<Args>(args)...);
    }
*/

    template<typename Obj,  typename...Ts, typename... RArgs>
    void register_handler(const std::string& tag, Obj* p, void (Obj::*f)(Ts...), RArgs&&... real_args)
    {
        if(sizeof...(real_args))
        {
            std::cout << "register_handler>>\n";
            using tu_t = decltype(std::make_tuple(std::forward<RArgs>(real_args)...));
            tu_t tu;
            std::any any = std::make_any<tu_t>(std::move(tu));
            std::cout << "register_handler: " << any.type().name() << "\n";
        }
        auto fun = [this, p, f](std::any const& params)
        {
            std::cout << "fun:" << params.type().name() << "\n";
//            std::cout << "any2tuple >>\n";
            std::any any_copy = params;
            decltype(auto) tux = any2tuple<Ts...>(any_copy);
//            std::cout << "<<any2tuple\n";
            decltype(auto) tu = std::tuple_cat(
//                        std::forward_as_tuple(std::forward<Obj>(p)),
                        std::tuple<Obj*>(p),
                        tux
                        );
//            std::tuple<Obj*, Ts...> tu1 = std::forward_as_tuple(p,);

//            std::tuple<

            std::tuple<Ts...> tut1 = any2tuple<Ts...>(any_copy);
            std::tuple<Obj*> tut2(p);
//            std::tuple<Obj*, Ts...> tu1{ std::tuple_cat(tut2, tut1) };
//            auto tu3 = tu;
            std::apply(f, std::move(tu));

///////an experiment
            struct A
            {
                void fn(std::string&& s)
                {
                    std::cout << s << "\n";
                }
            };

            auto fn = [](std::string&& s)
            {
                std::cout << s << "\n";
            };
            std::tuple<std::string&&> pr{std::string("my s")};
            std::string prs = "my par s";
//            std::invoke<decltype(f),std::string&&>(f,std::move(prs));
//            std::invoke(fn,std::move(prs));
            std::apply(fn,std::move(pr));

            A a;
            std::tuple<A*,std::string> tup1{&a, std::string("my s")};
            //std::tuple<A*,std::string&&> tup1{&a, std::string("my s")}; //this cannot be inserted into any
            std::any any(std::move(tup1));//< = std::make_any<A*,std::string>(&a,std::string("my s1"));

            std::tuple<A*,std::string&&> tpars{&a, std::string("my s")};
            void (A::*pmf)(std::string&&) = &A::fn;
            std::apply(pmf,std::move(tpars));

            std::any any1(pmf); // = std::make_any(fn);
            std::cout << any1.type().name() << "\n";
            auto pmf1 = std::any_cast<decltype(pmf)>(any1);

            (a.*pmf1)(std::string("uuu"));
//////////////
        };

        if(sizeof...(real_args))
        {
            auto any = args2any(real_args...);
            std::cout << "register_handler real_args: " << any.type().name() << "\n";
            fun(any);
        }

        m_members[tuple2index<Ts...>()][tag] = fun;

        if(sizeof...(real_args))
        {
            invoke(tag, real_args...);
            std::cout << "<<register_handler\n";
        }
    }

protected:
public:
    using handler_t = std::function<void (std::any const&)>;
    using tagged_handlers_t = std::map<handler_tag_t, handler_t>;
    using members_t = std::map<std::type_index, tagged_handlers_t>;
    template<typename... Args>
//    using make_tuple_type_t = decltype( std::make_tuple(std::declval<Args&&>()...) );
    using make_tuple_type_t = std::tuple<Args...>;

    template<class... Args>
    std::any args2any(Args... args)
    {
//        std::tuple<Args...> tu(args...);
//        auto tu = std::make_tuple(std::forward<Args>(args)...);
//        auto tu = std::forward_as_tuple(std::forward<Args>(args)...);
//        return std::make_any<decltype(tu)>(std::move(tu));
        return std::make_any<std::tuple<Args...>>(std::forward_as_tuple(args...));
    }

    template<typename... Args>
    std::type_index tuple2index()
    {
        return typeid(make_tuple_type_t<Args...>);
    }

    template<class... Args>
    decltype(auto) any2tuple(std::any const& a)
    {
        std::cout << "any2tuple: " << a.type().name() << "\n";
        return std::any_cast<make_tuple_type_t<Args...>>(a);
    }
#if 0
    class Holder
    {
    private:
        struct Base {  virtual ~Base(); };

        template <typename... Args>
        struct Wrapped : Base
        {
            using FuncType = std::function<int(Args...)>;
            FuncType f;
            Wrapped(FuncType func) : f(func) { };
        };
        std::unique_ptr<Base> m_basePtr;
    public:
        template <typename T> struct identity
        {
             using type = T;
        };

        template <typename... Args>
        Holder(typename identity<std::function<int(Args...)>>::type func)
            : m_basePtr(new Wrapped<Args...>(func)) { };

        template <typename... Args>
        Holder& operator=(typename identity<std::function<int(Args...)>>::type func)
        {
            m_basePtr = std::unique_ptr<Base>(new Wrapped<Args...>(func));
            return *this;
        };

        template <typename... Args>
        int operator()(Args... args) const
        {
            auto w = dynamic_cast< Wrapped<Args...>* >(m_basePtr.get());
            if(w) return w->f(args...);
            else throw std::runtime_error("Invalid arguments to function object call!");
        };
    };

    std::map<std::string, Holder> m_members;
#endif
private:
    members_t m_members;
    std::string m_name;
};

