#pragma once

namespace graft
{

enum class Status : int
{
    None,
    Ok,
    Forward,
    Error,
    Drop,
    Busy,
    InternalError,
    Stop, //for timer events
};

}//namespace graft
