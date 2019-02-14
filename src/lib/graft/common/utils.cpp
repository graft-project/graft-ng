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

}
}
