
#pragma once

#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>

#include <string>
#include <type_traits>
#include <utility>
#include <functional>
#include <thread>
#include <mutex>
#include <map>
#include <vector>
#include <chrono>
#include <any>

#include "lib/graft/graft_utility.hpp"
#include "lib/graft/graft_constants.h"

namespace graft { class ConfigOpts; }
namespace graft::request::system_info { class Counter; }

namespace graft
{
class HandlerAPI;

class GlobalContextMap : public TSHashtable<std::string, std::any>
{
public:
    GlobalContextMap(HandlerAPI* handlerAPI = nullptr) : m_handlerAPI(handlerAPI) { }
protected:
    HandlerAPI* m_handlerAPI;
};

class GlobalContextMapFriend : protected GlobalContextMap
{
public:
    GlobalContextMapFriend() = delete;
    static void cleanup(GlobalContextMap& gcm)
    {
        GlobalContextMapFriend& gcmf = static_cast<GlobalContextMapFriend&>(gcm);
        TSHashtable<std::string, std::any>& ht = gcmf;
        ht.cleanup();
    }
    static HandlerAPI* handlerAPI(GlobalContextMap& gcm)
    {
        GlobalContextMapFriend& gcmf = static_cast<GlobalContextMapFriend&>(gcm);
        return gcmf.m_handlerAPI;
    }
};

using SysInfoCounter = request::system_info::Counter;

class Context
{
public:
    class Local
    {
    private:
        using ContextMap = std::map<std::string, std::any>;
        ContextMap m_map;

        class Proxy
        {
        public:
            Proxy(ContextMap& map, const std::string& key)
                : m_map(map), m_key(key) { }

            template<typename T>
            Proxy& operator =(T&& v)
            {
                static_assert(std::is_nothrow_move_constructible<T>::value,
                              "not move constructible");

                std::any tmp(std::forward<T>(v));
                auto it = m_map.find(m_key);
                if(it == m_map.end())
                {
                    m_map.emplace(m_key, std::move(tmp));
                }
                else
                {
                    it->second = std::move(tmp);
                }
                return *this;
            }

            template<typename T>
            operator T& () const
            {
                return std::any_cast<T&>(m_map[m_key]);
            }

        private:
            ContextMap& m_map;
            const std::string& m_key;
        };

    public:
        Local() = default;
        ~Local() = default;
        Local(const Local&) = delete;
        Local(Local&&) = delete;

        template<typename T>
        T const& operator[](const std::string& key) const
        {
            auto it = m_map.find(key);
            return std::any_cast<T&>(it->second);
        }

        template<typename T>
        T operator[](const std::string& key) const
        {
            auto it = m_map.find(key);
            return std::any_cast<T>(it->second);
        }

        Proxy operator[](const std::string& key)
        {
            return Proxy(m_map, key);
        }

        bool hasKey(const std::string& key)
        {
            return (m_map.find(key) != m_map.end());
        }
        void remove(const std::string& key)
        {
            m_map.erase(key);
        }
        void setError(const char* str, Status status = Status::InternalError)
        {
            m_error = str;
            m_last_status = status;
        }
        Status getLastStatus() const
        {
            return m_last_status;
        }
        const std::string& getLastError() const
        {
            return m_error;
        }
    protected:
        Status m_last_status = Status::None;
        std::string m_error;
    };

    class LocalFriend : protected Local
    {
    public:
        LocalFriend() = delete;
        static void setLastStatus(Local& local, Status status)
        {
            LocalFriend& lf = static_cast<LocalFriend&>(local);
            lf.m_last_status = status;
        }
    };

    class Global
    {
    protected:
        GlobalContextMap& m_map;
    private:
        using GroupPtr = GlobalContextMap::GroupPtr;
        using NodePtr = GlobalContextMap::Group::NodePtr;

        class Proxy
        {
        public:
            Proxy(GlobalContextMap& map, const std::string& key)
                : m_map(map), m_key(key) { }

            template<typename T>
            Proxy& operator =(T&& v)
            {
                static_assert(std::is_nothrow_move_constructible<T>::value,
                              "not move constructible");
                std::any tmp(std::forward<T>(v));
                m_map.addOrUpdate(m_key, std::move(tmp));
                return *this;
            }

            template<typename T>
            operator T () const
            {
                return std::any_cast<T>(
                            m_map.valueFor(m_key, std::any()));
            }

        private:
            GlobalContextMap& m_map;
            const std::string& m_key;
        };

        GroupPtr getGroup(const std::string& gname)
        {
            return m_map.getGroup(gname);
        }

    public:

        Global(GlobalContextMap& map) : m_map(map) {}
        ~Global() = default;
        Global(const Global&) = delete;
        Global(Global&&) = delete;

        GlobalContextMap& getGcm() { return m_map; }

#if 0
        //currently broken; needs to be fixed
        template<typename T>
        T operator[](const std::string& key) const
        {
            return std::any_cast<T>(
                        m_map.valueFor(key, std::any()));
        }
#endif
        Proxy operator[](const std::string& key)
        {
            return Proxy(m_map, key);
        }

        bool hasKey(const std::string& key)
        {
            return m_map.hasKey(key);
        }

        template<typename T>
        void set(const std::string& key, T&& val, std::chrono::seconds ttl, GlobalContextMap::OnExpired onExpired = nullptr)
        {
            static_assert(std::is_nothrow_move_constructible<T>::value,
                          "not move constructible");
            std::any tmp(std::forward<T>(val));
            m_map.addOrUpdate(key, std::move(tmp), ttl, onExpired);
        }

        template<typename T>
        T get(const std::string& key, T defval) const
        {
            return std::any_cast<T>(
                m_map.valueFor(
                    key, std::any(std::forward<T>(defval))
                )
            );
        }

        template<typename T>
        bool apply(const std::string& key, std::function<bool(T&)> f)
        {
            return m_map.apply(key, [f](std::any& a)
            { return f(std::any_cast<T&>(a)); }
            );
        }

        void remove(const std::string& key)
        {
            return m_map.remove(key);
        }

        bool createGroup(const std::string& gname)
        {
            return m_map.createGroup(gname);
        }

        bool hasGroup(const std::string& gname)
        {
            return getGroup(gname) != nullptr;
        }

        bool deleteGroup(const std::string& gname)
        {
            return m_map.deleteGroup(gname);
        }

        bool groupAddKey(const std::string& gname, const std::string& key)
        {
            GroupPtr gptr = getGroup(gname);
            if(!gptr) return false;
            return gptr->add(key);
        }

        bool groupHasKey(const std::string& gname, const std::string& key)
        {
            GroupPtr gptr = getGroup(gname);
            if(!gptr) return false;
            return gptr->has(key);
        }

        bool groupRemoveKey(const std::string& gname, const std::string& key)
        {
            GroupPtr gptr = getGroup(gname);
            if(!gptr) return false;
            return gptr->remove(key);
        }

        //This method has exactly the same functionality as set<T> method, in some cases the repformance might be better (?)
        template<typename T>
        bool groupSet(const std::string& gname, const std::string& key, T&& val, std::chrono::seconds ttl = std::chrono::seconds(0), GlobalContextMap::OnExpired onExpired = nullptr)
        {
            GroupPtr gptr = getGroup(gname);
            if(!gptr) return false;
            NodePtr nptr = gptr->get(key);
            if(!nptr) return false;

            std::lock_guard<decltype(nptr->m)> lk(nptr->m);
            std::any& any = nptr->data->second;
            any = std::any(std::forward<T>(val));
            nptr->ttl = ttl;
            nptr->onExpired = onExpired;
            return true;
        }

        //This method has exactly the same functionality as get<T> method, in some cases the repformance might be better (?)
        template<typename T>
        T groupGet(const std::string& gname, const std::string& key, T&& defval)
        {
            GroupPtr gptr = getGroup(gname);
            if(!gptr) return std::forward<T>(defval);
            NodePtr nptr = gptr->get(key);
            if(!nptr) return std::forward<T>(defval);

            std::lock_guard<decltype(nptr->m)> lk(nptr->m);
            return std::any_cast<T>(nptr->data->second);
        }

        bool groupForEach(const std::string& gname, std::function<bool(const std::string& key, std::any& any)> f)
        {
            GroupPtr gptr = getGroup(gname);
            if(!gptr) return false;
            gptr->forEach(f);
            return true;
        }
    };

    class GlobalFriend : protected Global
    {
    public:
        GlobalFriend() = delete;
        static void cleanup(Global& global, bool all = false)
        {
            GlobalFriend& gf = static_cast<GlobalFriend&>(global);
            gf.m_map.cleanup(all);
        }
        static HandlerAPI* handlerAPI(Global& global)
        {
            GlobalFriend& gf = static_cast<GlobalFriend&>(global);
            return GlobalContextMapFriend::handlerAPI(gf.m_map);
        }
    };

    using uuid_t = boost::uuids::uuid;
    Context(GlobalContextMap& map)
        : global(map)
        , m_uuid(boost::uuids::nil_generator()())
        , m_nextUuid(boost::uuids::nil_generator()())
    {
    }

    Local local;
    Global global;

    void setCallback() const { getId(); }
    uuid_t getId(bool generateIfNil = true) const
    {
        if(generateIfNil && m_uuid.is_nil()) m_uuid = boost::uuids::random_generator()();
        return m_uuid;
    }
    void setNextTaskId(uuid_t uuid) { m_nextUuid = uuid; }
    uuid_t getNextTaskId() const { return m_nextUuid; }

    HandlerAPI* handlerAPI() { return GlobalFriend::handlerAPI(global); }

private:
    mutable uuid_t m_uuid;
    uuid_t m_nextUuid;
};
}//namespace graft
