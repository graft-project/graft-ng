#include "utils/utils.h"
#include <locale>            // epee::string_coding uses std::locale but misses include
#include <string_coding.h>


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

namespace {
template<typename res>
bool split_impl(const std::string_view& in, char delim, res& first, res& second)
{
    size_t pos = in.find(delim);
    if(pos != std::string::npos)
    {
        first = in.substr(0, pos);
        second = in.substr(pos+1);
        return true;
    }
    first = in;
    second = std::string_view();
    return false;
}

} //namespace

bool split(const std::string_view& in, char delim, std::string_view& first, std::string_view& second)
{
    return split_impl(in, delim, first, second);
}

bool split(const std::string_view& in, char delim, std::string& first, std::string& second)
{
    return split_impl(in, delim, first, second);
}

} //namespace utils
} //namespace graft
