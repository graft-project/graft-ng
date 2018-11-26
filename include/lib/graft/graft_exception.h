#pragma once
#include <stdexcept>

namespace graft
{

class exit_error : public std::runtime_error
{
public:
    explicit exit_error(const std::string& s) : std::runtime_error(s) { }
    explicit exit_error(const char* s) : std::runtime_error(s) { }
};

}//namespace graft
