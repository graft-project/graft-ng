
#pragma once

#include <string>
#include <random>

namespace graft::utils {

std::string base64_decode(const std::string &encoded_data);
std::string base64_encode(const std::string &data);

template <typename T>
T random_number(T startRange, T endRange)
{
    std::random_device rd;
    std::mt19937 mt(rd());
    std::uniform_int_distribution<T> dist(startRange, endRange);
    return dist(mt);
}

}

