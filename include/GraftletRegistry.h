/*
* Copyright (c) 2014 Clark Cianfarini
* Copyright (c) 2018 The Graft Project
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

//define __GRAFTLET__ before including this file if you compile graftlet

#include <exception>
#include <functional>
#include <map>
#include <memory>
#include <typeindex>
#include <sstream>

#ifdef __GRAFTLET__

#define GRAFTLET_EXPORT __attribute__((visibility("default")))

#define GRAFTLET_DERRIVED(concrete, base) \
    static_assert(std::is_base_of< base , concrete>::value, "ERROR: GRAFTLET: Registered concrete type must be of base type.")

//not required
#define GRAFTLET_DEFAULT_CTOR(concrete) \
//    static_assert((std::is_default_constructible<concrete>::value), "ERROR: GRAFTLET: Concrete type is not default constructable.")

#define GRAFTLET_CHECKS(concrete, base) \
    GRAFTLET_DERRIVED(concrete, base); GRAFTLET_DEFAULT_CTOR(concrete)

#define GRAFTLET_EXPORTS_BEGIN(name, version) \
    GRAFTLET_PLUGIN_NAME(name) \
    GRAFTLET_PLUGIN_VERSION(version) \
    GRAFTLET_PLUGIN_CHECK_FW_VERSION() \
    extern "C" GRAFTLET_EXPORT const char* getBuildSignature() { return graftlet::getBuildSignature(); } \
    static std::unique_ptr<graftlet::GraftletRegistry> pr_ptr; \
    extern "C" GRAFTLET_EXPORT graftlet::GraftletRegistry* getGraftletRegistry() { \
        if(!pr_ptr) { pr_ptr = std::make_unique<graftlet::GraftletRegistry>();

#define GRAFTLET_PLUGIN(concrete, base, ...) \
        GRAFTLET_CHECKS(concrete, base); pr_ptr->registerGraftlet<concrete, base>(__VA_ARGS__);

#define GRAFTLET_EXPORTS_END \
        } return pr_ptr.get(); } // extern "C" GRAFTLET_EXPORT void closeGraftletRegistry() { if (pr) delete pr; }

#define GRAFTLET_PLUGIN_NAME(name) \
    extern "C" GRAFTLET_EXPORT const char* getGraftletName() { return name; }
#define GRAFTLET_PLUGIN_VERSION(version) \
    extern "C" GRAFTLET_EXPORT int getGraftletVersion() { return version; }
#define GRAFTLET_PLUGIN_CHECK_FW_VERSION() \
    extern "C" GRAFTLET_EXPORT bool checkFwVersion( int fwVersion );

#define GRAFTLET_PLUGIN_DEFAULT_CHECK_FW_VERSION(minversion) \
    extern "C" GRAFTLET_EXPORT bool checkFwVersion( int fwVersion ) { return minversion <= fwVersion; }

extern "C" GRAFTLET_EXPORT const char* getGraftletName();

#endif //ifdef __GRAFTLET__

namespace graftlet
{
#define GRAFTLET_MKVER(Mj,mi) ((Mj<<8)|mi)
#define GRAFTLET_Major(Ver) ((Ver>>8)&0xFF)
#define GRAFTLET_Minor(Ver) (Ver&0xFF)

#define __GRAFT_STRINGIFY(x) __GRAFT_STRINGIFY_(x)
#define __GRAFT_STRINGIFY_(x)  #x

#if defined(__GXX_ABI_VERSION)
    #if __GXX_ABI_VERSION >= 1002
        #define __GRAFT_COMPILER \
                "c++abi-gcc4-comp"
    #else
        #define __GRAFT_COMPILER \
                "c++abi-" __GRAFT_STRINGIFY(__GXX_ABI_VERSION)
    #endif
#elif defined(__GNUG__)
    #define __GRAFT_COMPILER "gcc-" \
            __GRAFT_STRINGIFY(__GNUC_) "." __GRAFT_STRINGIFY(__GNUC_MINOR_)
#elif defined(__VISUALC__)
    #if MSC_VER >= 1900 && MSC_VER < 2000
        #define __MSVC_ABI_VERSION 1900
    #else
        #define _MSVC_ABI_VERSION MSC_VER
    #endif
    #define _GRAFT_COMPILER "msvc-" __GRAFT_STRINGIFY(__MSVC_ABI_VERSION)
#elif defined(__INTEL_COMPILER)
    #define __GRAFT_COMPILER ",intel-c++"
#else
    #define __GRAFT_COMPILER
#endif

#if defined(_GLIBCXX_USE_CXX11_ABI) && (_GLIBCXX_USE_CXX11_ABI == 1)
    #define GRAFT_BUILD_SIGNATURE __GRAFT_COMPILER ",glibc++-c++11-comp"
#else
    #define GRAFT_BUILD_SIGNATURE __GRAFT_COMPILER
#endif

class GraftletRegistry final
{
public:
    GraftletRegistry() = default;
    ~GraftletRegistry() = default;
    GraftletRegistry(const GraftletRegistry&) = delete;
    GraftletRegistry operator = (const GraftletRegistry&) = delete;
#ifdef __GRAFTLET__
    template <class T, class BaseT, class ...Args>
    void registerGraftlet(Args...args)
    {
        if (!m_graftlets.emplace(std::type_index(typeid(BaseT)), [this, args...]() { return std::make_shared<T>(args...); }).second)
            throw std::runtime_error("ERROR: Base type already defined in this plugin registry.");
    }

    bool empty() const { return m_graftlets.empty(); }
#else //__GRAFTLET__
    template <class BaseT>
    std::shared_ptr<BaseT> resolveGraftlet()
    {
        if (m_graftlets.find(std::type_index(typeid(BaseT))) != m_graftlets.end())
            return std::static_pointer_cast<BaseT>(m_graftlets[std::type_index(typeid(BaseT))]());

        return std::shared_ptr<BaseT>(nullptr);
    }
#endif //__GRAFTLET__
private:
    std::map<std::type_index, std::function<std::shared_ptr<void>()>> m_graftlets;
};

static inline const char* getBuildSignature()
{
    static std::string abi = GRAFT_BUILD_SIGNATURE;
    return abi.c_str();
}

static const char* getGraftletName();
static int getGraftletVersion();
static bool checkFwVersion( int fwVersion );
static GraftletRegistry* getGraftletRegistry();

inline std::string getVersionStr(int version)
{
    std::ostringstream oss;
    oss << GRAFTLET_Major(version) << "." << GRAFTLET_Minor(version);
    return oss.str();
}

#ifdef __GRAFTLET__

static const char* getGraftletName()
{
    return ::getGraftletName();
}

#endif //__GRAFTLET__

} //namespace graftlet

