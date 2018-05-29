#pragma once

#include <utility>
#include <string>
#include <vector>
#include <tuple>
#include <unordered_map>
#include <boost/hana.hpp>

#include "reflective-rapidjson/reflector-boosthana.h"
#include "reflective-rapidjson/serializable.h"
#include "reflective-rapidjson/types.h"

#include "graft_macros.h"

struct http_message; //from mongoose.h
struct mg_str; //from mongoose.h

#define GRAFT_DEFINE_IO_STRUCT(__S__, ...) \
    struct __S__ : public ReflectiveRapidJSON::JsonSerializable<__S__> { \
	BOOST_HANA_DEFINE_STRUCT(__S__, __VA_ARGS__); \
    }

#define GRAFT_DEFINE_IO_STRUCT_INITED(__S__, ...) \
    struct __S__ : public ReflectiveRapidJSON::JsonSerializable<__S__> { \
        __S__() : INIT_PAIRS(__VA_ARGS__) {} \
	BOOST_HANA_DEFINE_STRUCT(__S__, TN_PAIRS(__VA_ARGS__)); \
    }

/*
 *  Mapping of supported C++ types to supported JSON types
 *  ========================================================================
 *                           C++ type                        |  JSON type
 *  ---------------------------------------------------------+--------------
 *   custom structures/classes                               | object
 *   bool                                                    | true/false
 *   signed and unsigned integral types                      | number
 *   float and double                                        | number
 *   enum and enum class                                     | number
 *   std::string                                             | string
 *   const char *                                            | string
 *   iteratable lists (std::vector, std::list, ...)          | array
 *   sets (std::set, std::unordered_set, std::multiset, ...) | array
 *   std::tuple                                              | array
 *   std::unique_ptr, std::shared_ptr                        | depends/null
 *   std::map, std::unordered_map                            | object
 *   JsonSerializable                                        | object
 *  ---------------------------------------------------------+--------------
 *
 *  Example of structure definitions:
 *  =================================
 *
 *  GRAFT_DEFINE_IO_STRUCT(Payment,
 *      (uint64, amount),
 *      (uint32, block_height),
 *      (std::string, payment_id),
 *      (std::string, tx_hash),
 *      (uint32, unlock_time)
 * );
 *
 * or initialized with default values
 *
 *  GRAFT_DEFINE_IO_STRUCT_INITED(Payment,
 *      (uint64, amount, 999),
 *      (uint32, block_height, 10000),
 *      (std::string, payment_id, "abc"),
 *      (std::string, tx_hash, "def"),
 *      (uint32, unlock_time, 555555)
 * );
 *
 * GRAFT_DEFINE_IO_STRUCT(Payments,
 *     (std::vector<Payment>, payments)
 * );
 */

namespace graft 
{
    namespace serializer
    {
        template<typename T>
        struct JSON
        {
            static std::string serialize(const T& t)
            {
                return t.toJson().GetString();
            }
            static void deserialize(const std::string& s, T& t)
            {
                t = T::fromJson(s);
            }
        };
    } //namespace serializer

    class InOutHttpBase
    {
    protected:
        InOutHttpBase() = default;
        InOutHttpBase(const InOutHttpBase& ) = default;
        InOutHttpBase(InOutHttpBase&& ) = default;
        InOutHttpBase& operator = (InOutHttpBase&& ) = default;
        ~InOutHttpBase() = default;

        InOutHttpBase(const http_message& hm) { operator =(hm); }
        InOutHttpBase& operator = (const http_message& hm);
    public:
        InOutHttpBase& operator = (const InOutHttpBase& ) = default;
    public:
        void reset() { *this = InOutHttpBase(); }
        std::string combine_headers();
    public:
        //These fields are from mongoose http_message
        std::string body;
        std::string method;
        std::string uri;
        std::string proto;
        int resp_code;
        std::string resp_status_msg;
        std::string query_string;
        //Both headers and extra_headers deal with HTTP headers.
        //headers is name-value pairs.
        //extra_headers looks like "Content-Type: text/plane\r\nHeaderName: HeaderValue\r\n..."
        //When they are part of Input, and the Input is the result of a client request or
        //upstream response, extra_headers is empty and headers is filled accordingly.
        //When they are part of Output, and it is requested to do upstream forward,
        //the framework will combine resulting headers as
        //extra_headers + "name0: value0\r\n" + "name1: value1\r\n" ...;
        //The resulting value will be passed to mongoose
        //So it is preferable that worker_action makes Output so that headers is empty and
        //extra_headers is set to required complete value. This is for performance purpose.
        //You can use combine_headers() to do this, like following
        //  output.extra_headers = output.combine_headers();
        //  output.headers.clear();
        std::vector<std::pair<std::string, std::string>> headers;
        std::string extra_headers;
    private:
        void set_str_field(const http_message& hm, const mg_str& str_fld, std::string& fld);
    };

    class OutHttp final : public InOutHttpBase
    {
    public:
        OutHttp() = default;
        OutHttp(const OutHttp&) = default;
        OutHttp(OutHttp&&) = default;
        OutHttp& operator = (const OutHttp&) = default;
        OutHttp& operator = (OutHttp&&) = default;
        ~OutHttp() = default;

        template<typename T, typename S = serializer::JSON<T>>
        void load(const T& t)
        {
            body = S::serialize(t);
        }

        template<template<typename> typename S = serializer::JSON, typename T>
        void loadT(const T& t)
        {
            body = S<T>::serialize(t);
        }

        std::pair<const char *, size_t> get() const
        {
            return std::make_pair(body.c_str(), body.length());
        }

        std::string data() const
        {
            return body;
        }
    public:
        std::string makeUri(const std::string& default_uri) const;
    public:
        std::string host;
        std::string port;
        static std::unordered_map<std::string, std::string> uri_substitutions;
    };

    class InHttp final : public InOutHttpBase
    {
    public:
        InHttp() = default;
        InHttp(const InHttp&) = default;
        InHttp(InHttp&&) = default;
        InHttp& operator = (const InHttp&) = default;
        InHttp& operator = (InHttp&&) = default;
        ~InHttp() = default;
        InHttp(const http_message& hm) : InOutHttpBase(hm)
        { }

        template<typename T, typename S = serializer::JSON<T>>
        T get() const
        {
            T t;
            S::deserialize(body, t);
            return t;
        }

        template<template<typename> typename S = serializer::JSON, typename T>
        T getT() const
        {
            T t;
            S<T>::deserialize(body, t);
            return t;
        }

        void load(const char *buf, size_t size)
        {
            InOutHttpBase::reset();
            body.assign(buf, buf + size);
        }

        void assign(const OutHttp& out)
        {
            static_cast<InOutHttpBase&>(*this) = static_cast<const InOutHttpBase&>(out);
        }

        void reset()
        {
            InOutHttpBase::reset();
        }
    };

    using Input = InHttp;
    using Output = OutHttp;
} //namespace graft

