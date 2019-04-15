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

#include "lib/graft/ConfigIni.h"
#include "lib/graft/graft_exception.h"
#include <boost/property_tree/ini_parser.hpp>
#include <boost/algorithm/string/trim.hpp>

namespace graft
{

namespace
{

std::string trim_comments(std::string s)
{
    //remove ;; tail
    std::size_t pos = s.find(";;");
    if(pos != std::string::npos)
    {
      s = s.substr(0,pos);
    }
    boost::trim_right(s);
    return s;
}

class VConfigIniSubtreeImpl : public hidden::VConfigIniSubtree
{
public:
    boost::property_tree::ptree& ptree;

    template<typename T>
    T get_except(const std::string& path) const
    {
        try
        {
            return ptree.get<T>(path);
        }
        catch(std::exception& ex)
        {
            throw graft::exit_error(ex.what());
        }
    }
public:
    virtual bool get_bool(const std::string& path) const override
    {
        return get_except<bool>(path);
    }
    virtual long long get_long_long(const std::string& path) const override
    {
        return get_except<long long>(path);
    }
    virtual unsigned long long get_u_long_long(const std::string& path) const override
    {
        return get_except<unsigned long long>(path);
    }
    virtual long double get_long_double(const std::string& path) const override
    {
        return get_except<long double>(path);
    }
    virtual std::string get_string(const std::string& path) const override
    {
        return trim_comments( get_except<std::string>(path) );
    }

    virtual std::unique_ptr<hidden::VConfigIniSubtree> clone() const override
    {
        return std::make_unique<VConfigIniSubtreeImpl>(*this);
    }

    boost::property_tree::ptree& get_child(const std::string& path) const
    {
        return ptree.get_child(path);
    }
public:
    VConfigIniSubtreeImpl(boost::property_tree::ptree& ptree) : ptree(ptree) { }

    static VConfigIniSubtreeImpl* cast(hidden::VConfigIniSubtree* v)
    {
        assert(dynamic_cast<VConfigIniSubtreeImpl*>(v));
        return static_cast<VConfigIniSubtreeImpl*>(v);
    }
};

class VIterImpl : public hidden::VIter
{
    boost::property_tree::ptree::iterator iter;
public:
    VIterImpl(boost::property_tree::ptree::iterator&& iter) : iter(std::move(iter)) { }
    bool operator ==(const VIterImpl& it) const
    {
        return iter == it.iter;
    }
    virtual bool operator ==(const hidden::VIter& it) const override
    {
        assert(dynamic_cast<const VIterImpl*>(&it));
        return operator ==( static_cast<const VIterImpl&>(it) );
    }
    virtual hidden::VIter& operator++() override //prefix increment
    {
        ++iter;
        return *this;
    }
//    virtual hidden::VIter& operator++(int) = 0; //postfix increment
    virtual ConfigIniSubtreeRef operator*() const override
    {
        return ConfigIniSubtreeRef(std::make_unique<VConfigIniSubtreeImpl>(iter->second), std::string(iter->first) );
    }
};

class ConfigIni : public VConfigIniSubtreeImpl
{
    boost::property_tree::ptree ptree;
public:
    ConfigIni(const std::string& config_filename) : VConfigIniSubtreeImpl(ptree)
    {
        boost::property_tree::ini_parser::read_ini(config_filename, ptree);
    }
};

} //namespace


ConfigIniSubtree ConfigIniSubtree::get_child(const std::string& path) const
{
    const VConfigIniSubtreeImpl* vcisi = VConfigIniSubtreeImpl::cast(v.get());
    return ConfigIniSubtree( std::make_unique<VConfigIniSubtreeImpl>( vcisi->get_child(path) ) );
}

ConfigIniSubtree ConfigIniSubtree::create(const std::string& config_filename)
{
    return ConfigIniSubtree( std::make_unique<ConfigIni>(config_filename) );
}

ConfigIniSubtree::iterator ConfigIniSubtree::begin()
{
    const VConfigIniSubtreeImpl* vcisi = VConfigIniSubtreeImpl::cast(v.get());
    return ConfigIniSubtree::iterator( std::make_unique<VIterImpl>(vcisi->ptree.begin()) );
}

ConfigIniSubtree::iterator ConfigIniSubtree::end()
{
    const VConfigIniSubtreeImpl* vcisi = VConfigIniSubtreeImpl::cast(v.get());
    return ConfigIniSubtree::iterator( std::make_unique<VIterImpl>(vcisi->ptree.end()) );
}

ConfigIniSubtree ConfigIniSubtreeRef::value() const
{
    assert(dynamic_cast<VConfigIniSubtreeImpl*>(v.get()));
    std::unique_ptr<hidden::VConfigIniSubtree> nv = v->clone();
    return ConfigIniSubtree(std::move(nv));
}

} //namespace graft
