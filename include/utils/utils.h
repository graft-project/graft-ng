#ifndef UTILS_H
#define UTILS_H

#include <string>

namespace graft {
namespace utils {

std::string base64_decode(const std::string &encoded_data);
std::string base64_encode(const std::string &data);

}
}

#endif // UTILS_H
