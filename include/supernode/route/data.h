
#pragma once

#include <functional>
#include <map>
#include <string>

namespace graft {
enum class Status: int;
class Context;
class InHttp;
class OutHttp;
}

namespace graft::supernode::route {

using Vars = std::multimap<std::string, std::string>;
using graft::Context;
using graft::Status;

template<typename In, typename Out>
using HandlerT = std::function<Status (const Vars&, const In&, Context&, Out&) >;

using Input = graft::InHttp;
using Output = graft::OutHttp;

using Handler = HandlerT<Input, Output>;

}





