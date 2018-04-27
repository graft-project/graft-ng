#pragma once

#include <utility>
#include <iostream>
#include <vector>
#include <tuple>

namespace graft 
{
template <typename T>
class Out
{
public:
    Out() = default;
    virtual void load(const T& out) = 0;
    std::pair<const char *, size_t> get() const
    {
        return std::make_pair(m_v.data(), m_v.size());
    }
protected:
    std::vector<char> m_v;
};

class OutString : public Out<std::string>
{
public:
    using Out<std::string>::Out;
    void load(const std::string& out)
    {
        m_v.clear();
        std::copy(out.begin(), out.end(), std::back_inserter(m_v));
    }
};

template <typename T>
class In
{
public:
    void load(const char *buf, size_t size) { m_v.assign(buf, buf + size); }
    virtual T get() const = 0;

    void assign(const Out<T>& o)
    {
        const char *buf; size_t size;
        std::tie(buf, size) = o.get();
        load(buf, size);
    }
protected:
    std::vector<char> m_v;
};

class InString : public In<std::string>
{
public:
    std::string get() const
    {
        return std::string(m_v.begin(), m_v.end());
    }

};

using Input = InString;
using Output = OutString;

} //namespace graft
