
#pragma once

#include <string>
#include <random>

namespace graft::utils {

std::string base64_decode(const std::string &encoded_data);
std::string base64_encode(const std::string &data);

template <typename T>
T random_number(T startRange, T endRange)
{
    static std::mt19937 mt(std::random_device{}());
    std::uniform_int_distribution<T> dist(startRange, endRange);
    return dist(mt);
}

//non-cryptographic hash function
// the result should be interpreted as a 64bit number, it is correct for both little/big endian
uint64_t xorol(const uint8_t* buf, size_t len);

}

