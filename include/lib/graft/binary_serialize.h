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

#include <boost/hana.hpp>
#include <boost/archive/binary_oarchive.hpp>
#include <boost/archive/binary_iarchive.hpp>

#include "lib/graft/inout.h"
//#include "crypto/crypto.h"

namespace graft { namespace serializer {

namespace binary_details
{

template<typename ...>
using to_void = void; // maps everything to void, used in non-evaluated contexts

template<typename C, typename = void>
struct is_container : std::false_type
{};

template<typename C>
struct is_container<C,
        to_void<decltype(std::declval<C>().begin()),
                decltype(std::declval<C>().end()),
                typename C::value_type
        >> : std::true_type // will  be enabled for iterable objects
{};

static_assert( is_container<std::string>::value );
static_assert( is_container<std::vector<int>>::value );
///////////////////////
/*! \brief write_varint adopted from cryptonode.
 */
template<typename Arch, typename V>
// Requires T to be both an integral type and unsigned, should be a compile error if it is not
static void write_varint(Arch& ar, V i) {
  // Make sure that there is one after this
  while (i >= 0x80) {
    char ch = (static_cast<char>(i) & 0x7f) | 0x80;
//        ++dest;
    ar << ch;
    i >>= 7;			// I should be in multiples of 7, this should just get the next part
  }
  // writes the last one to dest
/*
  *dest = static_cast<char>(i);
  dest++;			// Seems kinda pointless...
*/
  ar << static_cast<char>(i);
}


/*! \brief read_varint adopted from cryptonode.
 */
template<typename Arch, typename V>
static bool read_varint(Arch& ar, V& write)
{
  constexpr int bits = std::numeric_limits<V>::digits;
  int read = 0;
  write = 0;
  for (int shift = 0;; shift += 7) {
/*
    if (first == last)
    {
        return read;
    }
*/
/*
    unsigned char byte = *first;
    ++first;
*/
    unsigned char byte;
    ar >> byte;

    ++read;

    if (shift + 7 >= bits && byte >= 1 << (bits - shift)) {
  return -1; //EVARINT_OVERFLOW;
    }
    if (byte == 0 && shift != 0) {
  return -2; //EVARINT_REPRESENT;
    }

    write |= static_cast<V>(byte & 0x7f) << shift; /* Does the actualy placing into write, stripping the first bit */

    /* If there is no next */
    if ((byte & 0x80) == 0) {
  break;
    }
  }
  return read;
}

//////////////
// forward declarations
template<typename Arch, typename V>
static void bserialize(Arch& ar, V& v);

template<typename Arch, typename V>
static void bdeserialize(Arch& ar, V& v);
//////////////
template<typename Arch, typename V>
static void ser(Arch& ar, V& v)
{
//    static_assert(std::is_same_v<std::const crypto::hash, crypto::hash>);
//    if constexpr(is_container<V>::value || std::is_same_v<V, crypto::hash>)
//    static_assert(std::is_trivially_copyable<crypto::public_key>::value && std::is_class<crypto::public_key>::value);
    static_assert(!(std::is_trivially_copyable<int>::value && std::is_class<int>::value));
    if constexpr(is_container<V>::value)
    {
        size_t size = v.size();
        write_varint(ar, size);
        std::for_each(v.begin(), v.end(), [&](auto& item)
        {
            using naked_member_t = std::remove_cv_t<std::remove_reference_t<decltype(item)>>;
            if constexpr(!std::is_base_of<ReflectiveRapidJSON::JsonSerializable<naked_member_t>, naked_member_t>::value)
            {
                ser(ar, item);
            }
            else
            {
                bserialize(ar, item);
            }
        });
    }
    else if constexpr(std::is_trivially_copyable<V>::value && std::is_class<V>::value)
    {
        const uint8_t* p = reinterpret_cast<const uint8_t*>(&v);
        for(int i=0; i<sizeof(V); ++i, ++p)
        {
            ar << *p;
        }
    }
    else
    {
        ar << v;
    }
}

template<typename Arch, typename V>
static void deser(Arch& ar, V& v)
{
    if constexpr(is_container<V>::value)
    {
        size_t size;
        read_varint(ar, size);
        v.resize(size);
        std::for_each(v.begin(), v.end(), [&](auto& item)
        {
            using naked_member_t = std::remove_cv_t<std::remove_reference_t<decltype(item)>>;
            if constexpr(!std::is_base_of<ReflectiveRapidJSON::JsonSerializable<naked_member_t>, naked_member_t>::value)
            {
                deser(ar, item);
            }
            else
            {
                bdeserialize(ar, item);
            }
        });
    }
    else if constexpr(std::is_trivially_copyable<V>::value && std::is_class<V>::value)
    {
        uint8_t* p = reinterpret_cast<uint8_t*>(&v);
        for(int i=0; i<sizeof(V); ++i, ++p)
        {
            ar >> *p;
        }
    }
    else
    {
        ar >> v;
    }
}

template<typename Arch, typename V>
static void bserialize(Arch& ar, V& v)
{
    boost::hana::for_each(boost::hana::keys(v), [&](auto key)
    {
        const auto& member = boost::hana::at_key(v, key);
        using naked_member_t = std::remove_cv_t<std::remove_reference_t<decltype(member)>>;
        if constexpr(!std::is_base_of<ReflectiveRapidJSON::JsonSerializable<naked_member_t>, naked_member_t>::value)
        {
            ser(ar, member);
        }
        else
        {
            bserialize(ar, member);
        }
    });
}

template<typename Arch, typename V>
static void bdeserialize(Arch& ar, V& v)
{
    boost::hana::for_each(boost::hana::keys(v), [&](auto key)
    {
        auto& member = boost::hana::at_key(v, key);
        using naked_member_t = std::remove_cv_t<std::remove_reference_t<decltype(member)>>;
        if constexpr(!std::is_base_of<ReflectiveRapidJSON::JsonSerializable<naked_member_t>, naked_member_t>::value)
        {
            deser(ar, member);
        }
        else
        {
            bdeserialize(ar, member);
        }
    });
}

} //namespace binary_details

//It is expected that serialize and deserialize throw exception on error
template<typename T>
class Binary
{
public:
    static std::string serialize(const T& t)
    {
        std::ostringstream oss;
        boost::archive::binary_oarchive ar(oss, boost::archive::no_header);
        binary_details::bserialize(ar, t);
        return oss.str();
    }

    static void deserialize(const std::string& s, T& t)
    {
        std::istringstream iss(s);
        boost::archive::binary_iarchive ar(iss, boost::archive::no_header);
        binary_details::bdeserialize(ar,t);
    }
};

} } //namespace graft { namespace serializer


