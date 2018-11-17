
#pragma once

#include <string>
#include <random>
#include <set>

namespace graft { class ConnectionManager; }

namespace graft::supernode::utils {

std::string base64_decode(const std::string& encoded_data);
std::string base64_encode(const std::string& data);

std::string get_home_dir(void);  // TODO: make it crossplatform
std::string trim_comments(std::string s);

template <typename T>
T random_number(T startRange, T endRange)
{
    std::random_device rd;
    std::mt19937 mt(rd());
    std::uniform_int_distribution<T> dist(startRange, endRange);
    return dist(mt);
}

void check_routes(graft::ConnectionManager& cm);
void split_string_by_separator(const std::string& src, char sep, std::set<std::string>& dst);
void remove_duplicates_from_vector(std::vector<std::string>& vec);

}

