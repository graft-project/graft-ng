// Copyright (c) 2018, The Graft Project
//
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without modification, are
// permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice, this list of
//    conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright notice, this list
//    of conditions and the following disclaimer in the documentation and/or other
//    materials provided with the distribution.
//
// 3. Neither the name of the copyright holder nor the names of its contributors may be
//    used to endorse or promote products derived from this software without specific
//    prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
// EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
// THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
// STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
// THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#pragma once

#include <string>
#include <optional>
#include <memory>
#include <type_traits>

namespace graft
{

class ConfigIniSubtree;
class ConfigIniSubtreeRef;

namespace hidden
{

class VConfigIniSubtree
{
public:
    virtual ~VConfigIniSubtree() = default;
    //get_... that throw exit_error
    virtual bool get_bool(const std::string& path) const = 0;
    virtual long long get_long_long(const std::string& path) const = 0;
    virtual unsigned long long get_u_long_long(const std::string& path) const = 0;
    virtual long double get_long_double(const std::string& path) const = 0;
    virtual std::string get_string(const std::string& path) const = 0;

    virtual std::unique_ptr<VConfigIniSubtree> clone() const = 0;
};

class VIter
{
protected:
    VIter() = default;
public:
    virtual ~VIter() = default;
    VIter(const VIter&) = delete;
    VIter& operator=(const VIter&) = delete;

    virtual bool operator == (const VIter& it) const = 0;
    virtual VIter& operator++() = 0; //prefix increment
//    virtual VIter& operator++(int) = 0; //postfix increment
    virtual ConfigIniSubtreeRef operator*() const = 0;
};

} //namespace hidden

class ConfigIniSubtreeRef final
{
    std::unique_ptr<hidden::VConfigIniSubtree> v;
    std::string n;
public:
    ~ConfigIniSubtreeRef() = default;
    ConfigIniSubtreeRef(const ConfigIniSubtreeRef& sr) : v(sr.v->clone()), n(sr.n) { }
    ConfigIniSubtreeRef& operator = (const ConfigIniSubtreeRef& sr) { v = sr.v->clone(); n = sr.n; return *this; }
    ConfigIniSubtreeRef(ConfigIniSubtreeRef&& sr) = default;
    ConfigIniSubtreeRef& operator = (ConfigIniSubtreeRef&& sr) = default;

    ConfigIniSubtreeRef(std::unique_ptr<hidden::VConfigIniSubtree>&& v, std::string&& n)
        : v(std::move(v))
        , n(std::move(n)) { }
    const std::string& name() const { return n; }
    ConfigIniSubtree value() const;
};

//Interface to get values from config.ini
class ConfigIniSubtree
{
private:

protected:
    std::unique_ptr<hidden::VConfigIniSubtree> v;
    ConfigIniSubtree(std::unique_ptr<hidden::VConfigIniSubtree>&& v) : v(std::move(v)) { }
    friend class ConfigIniSubtreeRef;
public:
    ~ConfigIniSubtree() = default;
    ConfigIniSubtree(const ConfigIniSubtree& s) : v( s.v->clone() ) {  }
    ConfigIniSubtree& operator =(const ConfigIniSubtree& s) { v = s.v->clone(); return *this; }
    ConfigIniSubtree(ConfigIniSubtree&& s) = default;
    ConfigIniSubtree& operator =(ConfigIniSubtree&& s) = default;

    static ConfigIniSubtree create(const std::string& config_filename);

    class iterator
    {
        std::unique_ptr<hidden::VIter> vi;
        friend ConfigIniSubtree;
    public:
        ~iterator() = default;
        iterator(const iterator& it);
        iterator& operator =(const iterator& it);
        iterator(iterator&& it) = default;
        iterator& operator =(iterator&& it) = default;

        iterator(std::unique_ptr<hidden::VIter>&& vi) : vi(std::move(vi)) { }

        bool operator ==(const iterator& it) const
        {
            vi->operator ==(*it.vi);
        }
        bool operator !=(const iterator& it) const
        {
            return ! operator ==(it);
        }

        iterator& operator ++() //prefix increment
        {
            vi->operator ++();
            return *this;
        }
//        iterator& operator++(int); //postfix increment
        ConfigIniSubtreeRef operator *() const
        {
            return vi->operator *();
        }
        std::unique_ptr<ConfigIniSubtreeRef> operator->() const
        {
            return std::make_unique<ConfigIniSubtreeRef>( operator *() );
        }
    };

    iterator begin();
    iterator end();

    //throw exit_error
    ConfigIniSubtree get_child(const std::string& path) const;

    std::optional<ConfigIniSubtree> get_child_optional(const std::string& path) const
    {
        try
        {
            ConfigIniSubtree res = get_child(path);
            return std::optional<ConfigIniSubtree>(std::move(res));
        }
        catch(std::exception&){ return std::optional<ConfigIniSubtree>(); }
    }

    //get functions that throw exit_error
    template<typename T>
    typename std::enable_if<std::is_same<T, bool>::value, T>::type
    get(const std::string& path) const
    {
        return v->get_bool(path);
    }

    template<typename T>
    typename std::enable_if<std::is_integral<T>::value && std::is_signed<T>::value && !std::is_same<T, bool>::value, T>::type
    get(const std::string& path) const
    {
        return static_cast<T>( v->get_long_long(path) );
    }

    template<typename T>
    typename std::enable_if<std::is_integral<T>::value && std::is_unsigned<T>::value && !std::is_same<T, bool>::value, T>::type
    get(const std::string& path) const
    {
        return static_cast<T>( v->get_u_long_long(path) );
    }

    template<typename T>
    typename std::enable_if<std::is_floating_point<T>::value, T>::type
    get(const std::string& path) const
    {
        return static_cast<T>( v->get_long_double(path) );
    }

    template<typename T>
    typename std::enable_if<std::is_same<T, std::string>::value, T>::type
    get(const std::string& path) const
    {
        return v->get_string(path);
    }

    //get functions with default
    template<typename T>
    T get(const std::string& path, const T& default_value) const
    {
        try{ return get<T>(path); }
        catch(std::exception&){ return default_value; }
    }

    //get functions with optional
    template<typename T>
    std::optional<T> get_optional(const std::string& path) const
    {
        try
        {
            T res = get<T>(path);
            return std::optional<T>(std::move(res));
        }
        catch(std::exception&){ return std::optional<T>(); }
    }
};

} //namespace graft
