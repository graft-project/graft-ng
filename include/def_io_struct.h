#pragma once

#include "reflective-rapidjson/reflector-boosthana.h"
#include "reflective-rapidjson/serializable.h"
#include "reflective-rapidjson/types.h"

#define GRAFT_DEFINE_IO_STRUCT(__S__, ...) \
    struct __S__ : public ReflectiveRapidJSON::JsonSerializable<__S__> { \
	BOOST_HANA_DEFINE_STRUCT(__S__, __VA_ARGS__); \
    }

#define GRAFT_DEFINE_IO_STRUCT_INITED(__S__, ...) \
    struct __S__ : public ReflectiveRapidJSON::JsonSerializable<__S__> { \
        __S__() : INIT_PAIRS(__VA_ARGS__) {} \
	BOOST_HANA_DEFINE_STRUCT(__S__, TN_PAIRS(__VA_ARGS__)); \
    }

