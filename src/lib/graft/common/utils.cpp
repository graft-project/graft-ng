#include "utils/utils.h"
#include <locale>            // epee::string_coding uses std::locale but misses include
#include <string_coding.h>
#include <boost/endian/conversion.hpp>

#if 0
//can be used to check manually
#define native_to_little boost::endian::native_to_big
#define little_to_native boost::endian::big_to_native
#else
using namespace boost::endian;
#endif


namespace graft {
namespace utils {

std::string base64_decode(const std::string &encoded_data)
{
    return epee::string_encoding::base64_decode(encoded_data);
}

std::string base64_encode(const std::string &data)
{
    return epee::string_encoding::base64_encode(data);
}

uint64_t xorol(const uint8_t* buf, size_t len)
{
    uint64_t res = uint64_t(len);
    {//tail
        const uint8_t* p = buf + len - 1;
        const uint8_t* pe = p - len%8;
        for(; p != pe; --p)
        {
            res <<= 8, res |= uint64_t(*p);
        }
    }
    {
        const uint64_t* p = reinterpret_cast<const uint64_t*>(buf);
        const uint64_t* pe = p + len/8;
        for(; p != pe; ++p)
        {
            res = (res<<1) | (res>>63), res ^= native_to_little(*p);
        }
    }
    return res;
}

}
}
