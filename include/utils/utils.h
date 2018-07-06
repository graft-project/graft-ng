#ifndef UTILS_H
#define UTILS_H

#include <string>

namespace graft {
namespace utils {

static std::string base64_decode(const std::string &encoded_data);
static std::string base64_encode(const std::string &data);

}
}

#endif // UTILS_H
