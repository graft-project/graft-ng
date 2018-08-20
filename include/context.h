#pragma once

#include <boost/any.hpp>
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

#include "graft_utility.hpp"
#include "graft_constants.h"

namespace graft { class ConfigOpts; }
namespace graft { namespace supernode { class SystemInfoProvider; } }

namespace graft
{
using GlobalContextMap = graft::TSHashtable<std::string, boost::any>;
using supernode::SystemInfoProvider;

class Context
{
public:
    const ConfigOpts& config_opts(void) const;
    void config_opts(const ConfigOpts& copts);

    SystemInfoProvider& runtime_sys_info(void);
    void runtime_sys_info(SystemInfoProvider& sip);

    class Local
    {
    private:
        using ContextMap = std::map<std::string, boost::any>;
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

                boost::any tmp(std::forward<T>(v));
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
                return boost::any_cast<T&>(m_map[m_key]);
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
            return boost::any_cast<T&>(it->second);
        }

        template<typename T>
        T operator[](const std::string& key) const
        {
            auto it = m_map.find(key);
            return boost::any_cast<T>(it->second);
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
                boost::any tmp(std::forward<T>(v));
                m_map.addOrUpdate(m_key, std::move(tmp));
                return *this;
            }

            template<typename T>
            operator T () const
            {
                return boost::any_cast<T>(
                            m_map.valueFor(m_key, boost::any()));
            }

        private:
            GlobalContextMap& m_map;
            const std::string& m_key;
        };

    public:
        Global(GlobalContextMap& map) : m_map(map) {}
        ~Global() = default;
        Global(const Global&) = delete;
        Global(Global&&) = delete;

        template<typename T>
        T operator[](const std::string& key) const
        {
            return boost::any_cast<T>(
                        m_map.valueFor(key, boost::any()));
        }

        Proxy operator[](const std::string& key)
        {
            return Proxy(m_map, key);
        }

        bool hasKey(const std::string& key)
        {
            return m_map.hasKey(key);
        }

        template<typename T>
        void set(const std::string& key, T&& val, std::chrono::seconds ttl)
        {
            static_assert(std::is_nothrow_move_constructible<T>::value,
                          "not move constructible");
            boost::any tmp(std::forward<T>(val));
            m_map.addOrUpdate(key, std::move(tmp), ttl);
        }

        template<typename T>
        T get(const std::string& key, T defval) const
        {
            return boost::any_cast<T>(
                m_map.valueFor(
                    key, boost::any(std::forward<T>(defval))
                )
            );
        }

        template<typename T>
        bool apply(const std::string& key, std::function<bool(T&)> f)
        {
            return m_map.apply(key, [f](boost::any& a)
            { return f(boost::any_cast<T&>(a)); }
            );
        }

        void remove(const std::string& key)
        {
            return m_map.remove(key);
        }
    };

    class GlobalFriend : protected Global
    {
    public:
        GlobalFriend() = delete;
        static void cleanup(Global& global)
        {
            GlobalFriend& gf = static_cast<GlobalFriend&>(global);
            gf.m_map.cleanup();
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

    uuid_t getId() const { if(m_uuid.is_nil()) m_uuid = boost::uuids::random_generator()(); return m_uuid; }
    void setNextTaskId(uuid_t uuid) { m_nextUuid = uuid; }
    uuid_t getNextTaskId() const { return m_nextUuid; }

private:
    mutable uuid_t m_uuid;
    uuid_t m_nextUuid;
};

}//namespace graft
