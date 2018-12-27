
#pragma once

#include "lib/graft/graft_macros.h"
#include "lib/graft/common/utils.h"

#include "lib/graft/reflective-rapidjson/reflector-boosthana.h"
#include "lib/graft/reflective-rapidjson/serializable.h"
#include "lib/graft/reflective-rapidjson/types.h"

#include <utility>
#include <string>
#include <vector>
#include <tuple>
#include <unordered_map>
#include <boost/hana.hpp>

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
        class JsonParseError : public std::runtime_error
        {
        public:
            JsonParseError(const rapidjson::ParseResult &pr)
                : std::runtime_error( std::string("Json parse error, code: ") + std::to_string(pr.Code())
                                  + ", offset: " + std::to_string(pr.Offset()))
            {
            }
        };


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

        template<typename T>
        struct JSON_B64
        {
            static std::string serialize(const T& t)
            {
                return utils::base64_encode(t.toJson().GetString());
            }
            static void deserialize(const std::string& s, T& t)
            {
                t = T::fromJson(utils::base64_decode(s));
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

        InOutHttpBase(const http_message& hm, const std::string& host_) { operator =(hm); host = host_; }
    public:
        InOutHttpBase& operator = (const InOutHttpBase& ) = default;

        void reset() { *this = InOutHttpBase(); }
        std::string combine_headers();

        //sometimes it is required to know client's host in a handler from input
        std::string host;
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
        InOutHttpBase& operator = (const http_message& hm);
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

        /*!
         * \brief makeUri - please DO NOT use it. It is for internal usage.
         * Set uri, proto, host, port, path members if you need.
         * The function forms real URI substituting absent parts according to Config.ini.
         * It is public to be accessed from tests and other classes.
         * \param default_uri - this parameter always comes from [cryptonode]rpc-address of Config.ini.
         * \return
         */
        std::string makeUri(const std::string& default_uri) const;

        std::string port;
        std::string path;
        static std::unordered_map<std::string, std::tuple<std::string,int,bool,double>> uri_substitutions;
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
        InHttp(const http_message& hm, const std::string& host_) : InOutHttpBase(hm, host_)
        { }

        /*!
         * \brief get - parses object from JSON. Throws ParseError exception in case parse error
         * \return   Object of type T
         */
        template<typename T>
        T get() const
        {
            T result;
            try {
                serializer::JSON<T>::deserialize(body, result);
            } catch (const rapidjson::ParseResult &pr) {
                throw serializer::JsonParseError(pr);
            }
            return result;
        }

        /*!
         * \brief get - overloaded method not throwning exception
         * \param result - reference to result object of type T
         * \return  - true on success
         */
        template<typename T>
        bool get(T& result) const
        {
            try {
                result = this->get<T>();
                return true;
            } catch (const serializer::JsonParseError & /*err*/) {
                return false;
            }
        }

        template<typename T, typename S>
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

        template<template<typename> typename S = serializer::JSON, typename T>
        bool getT(T &result) const
        {
            try {
                result = this->getT<S,T>();
                return true;
            } catch (const serializer::JsonParseError & /*err*/) {
                return false;
            } catch (...) {
                return false;
            }
        }

        void load(const char *buf, size_t size)
        {
            InOutHttpBase::reset();
            body.assign(buf, buf + size);
        }

        void load(const std::string &data)
        {
            this->load(data.c_str(), data.size());
        }

        void assign(const OutHttp& out)
        {
            static_cast<InOutHttpBase&>(*this) = static_cast<const InOutHttpBase&>(out);
        }

        void reset()
        {
            InOutHttpBase::reset();
        }

        std::string data() const
        {
            return body;
        }

    public:
        uint16_t port = 0;
    };

    using Input = InHttp;
    using Output = OutHttp;
} //namespace graft

