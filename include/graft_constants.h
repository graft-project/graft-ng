#pragma once

namespace graft
{

#define GRAFT_STATUS_LIST(EXP) \
    EXP(None) \
    EXP(Ok) \
    EXP(Forward) \
    EXP(Error) \
    EXP(Drop) \
    EXP(Busy) \
    EXP(InternalError) \
    EXP(Stop) //for timer events

#define EXP_TO_ENUM(x) x,
#define EXP_TO_STR(x) #x,

enum class Status : int { GRAFT_STATUS_LIST(EXP_TO_ENUM) };

}//namespace graft
