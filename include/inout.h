#pragma once

#include <utility>
#include <string>
#include <vector>
#include <tuple>
#include <boost/hana.hpp>

#include "reflective-rapidjson/reflector-boosthana.h"
#include "reflective-rapidjson/serializable.h"
#include "reflective-rapidjson/types.h"

#define GRAFT_DEFINE_IO_STRUCT(__S__, ...)              \
	struct __S__ : public ReflectiveRapidJSON::JsonSerializable<__S__> \
		{BOOST_HANA_DEFINE_STRUCT(__S__, __VA_ARGS__);}

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
 * GRAFT_DEFINE_IO_STRUCT(Payments,
 *     (std::vector<Payment>, payments)
 * );
 */

namespace graft 
{
    class OutJson
    {
    public:
        template <typename T>
        void load(const T& out)
        {
            m_buf.assign(out.toJson().GetString());
        }

        std::pair<const char *, size_t> get() const
        {
            return std::make_pair(m_buf.c_str(), m_buf.length());
        }

        std::string data() const
        {
            return m_buf;
        }
    private:
        std::string m_buf;
    };

    class InJson
    {
    public:
        template <typename T>
        T get() const
        {
            return T::fromJson(m_buf);
        }

        void load(const char *buf, size_t size) { m_buf.assign(buf, buf + size); }
        void load(const std::string &buf) { m_buf = buf; }

        void assign(const OutJson& o)
        {
            const char *buf; size_t size;
            std::tie(buf, size) = o.get();
            load(buf, size);
        }
    private:
        std::string m_buf;
    };

    using Input = InJson;
    using Output = OutJson;
} //namespace graft

