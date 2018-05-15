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
    InternalError,
};

}//namespace graft
