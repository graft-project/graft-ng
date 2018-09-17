#pragma once

#include <string>

namespace graft
{
//The function truncates binary data for output to trunc_size characters.
//trunc_size == -1 means no limit.
//Unreadable characters converted to escaped hexadecimal sequences or \r,\n,\t.
std::string make_dump_output(const std::string& in, int trunc_size);

}//namespace graft

