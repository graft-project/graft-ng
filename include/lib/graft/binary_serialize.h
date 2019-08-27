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
#include "lib/graft/reflective-rapidjson/serializable.h"

#include "lib/graft/inout.h"

namespace graft { namespace serializer {

namespace binary_details
{

template<typename T>
using naked_t = std::remove_cv_t< std::remove_reference_t<T> >;

template<typename ...>
using to_void = void; // maps everything to void, used in non-evaluated contexts

template<typename C, typename = void>
struct is_container_exact : std::false_type
{};

template<typename C>
struct is_container_exact<C,
        to_void<decltype(std::declval<C>().begin()),
                decltype(std::declval<C>().end()),
                typename C::value_type
        >> : std::true_type // will  be enabled for iterable objects
{};

template<typename T>
using is_container = is_container_exact< naked_t<T> >;

static_assert( is_container<std::string>::value );
static_assert( is_container<std::vector<int>>::value );
static_assert( is_container<const std::vector<int>>::value );
static_assert( is_container<const std::vector<int>&>::value );

template<typename T>
inline constexpr bool is_container_v = is_container<T>::value;

template<typename T>
inline constexpr bool is_serializable_v = std::is_base_of<ReflectiveRapidJSON::JsonSerializable<naked_t<T>>, naked_t<T>>::value;

template<typename T>
struct is_trivial_class { static constexpr bool value = std::is_trivially_copyable<naked_t<T>>::value && std::is_class<naked_t<T>>::value; };

template<typename T>
inline constexpr bool is_trivial_class_v = is_trivial_class<T>::value;

///////////////////////
/*! \brief write_varint adopted from cryptonode.
 */
template<typename Arch, typename V>
// Requires T to be both an integral type and unsigned, should be a compile error if it is not
void write_varint(Arch& ar, V i) {
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
bool read_varint(Arch& ar, V& write)
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
void bserialize(Arch& ar, V& v);

template<typename Arch, typename V>
void bdeserialize(Arch& ar, V& v);
//////////////

template<typename Arch, typename V>
typename std::enable_if< !is_serializable_v<V> && !is_container_v<V> && !is_trivial_class_v<V> >::type
ser(Arch& ar, V& v)
{
    ar << v;
}

template<typename Arch, typename V>
typename std::enable_if< !is_serializable_v<V> && !is_container_v<V> && is_trivial_class_v<V> >::type
ser(Arch& ar, V& v)
{
    const uint8_t* p = reinterpret_cast<const uint8_t*>(&v);
    for(int i=0; i<sizeof(V); ++i, ++p)
    {
        ar << *p;
    }
}

template<typename Arch, typename V>
typename std::enable_if< is_container_v<V> >::type //it means also !is_serializable_v<V>
ser(Arch& ar, V& v)
{
    size_t size = v.size();
    write_varint(ar, size);
    std::for_each(v.begin(), v.end(), [&](auto& item)
    {
        bserialize(ar, item);
    });
}

template<typename Arch, typename V>
typename std::enable_if< is_serializable_v<V> >::type
ser(Arch& ar, V& v)
{
    boost::hana::for_each(boost::hana::keys(v), [&](auto key)
    {
        const auto& member = boost::hana::at_key(v, key);
        ser(ar, member);
    });
}

template<typename Arch, typename V>
void bserialize(Arch& ar, V& v)
{
    ser(ar, v);
}

template<typename Arch, typename V>
typename std::enable_if< !is_serializable_v<V> && !is_container_v<V> && !is_trivial_class_v<V> >::type
deser(Arch& ar, V& v)
{
    ar >> v;
}

template<typename Arch, typename V>
typename std::enable_if< !is_serializable_v<V> && !is_container_v<V> && is_trivial_class_v<V> >::type
deser(Arch& ar, V& v)
{
    uint8_t* p = reinterpret_cast<uint8_t*>(&v);
    for(int i=0; i<sizeof(V); ++i, ++p)
    {
        ar >> *p;
    }
}

template<typename Arch, typename V>
typename std::enable_if< is_container_v<V> >::type //it means also !is_serializable_v<V>
deser(Arch& ar, V& v)
{
    size_t size;
    read_varint(ar, size);
    v.resize(size);
    std::for_each(v.begin(), v.end(), [&](auto& item)
    {
        bdeserialize(ar, item);
    });
}

template<typename Arch, typename V>
typename std::enable_if< is_serializable_v<V> >::type
deser(Arch& ar, V& v)
{
    boost::hana::for_each(boost::hana::keys(v), [&](auto key)
    {
        auto& member = boost::hana::at_key(v, key);
        deser(ar, member);
    });
}


template<typename Arch, typename V>
void bdeserialize(Arch& ar, V& v)
{
    deser(ar, v);
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


