#pragma once

#include <deque>
#include <string>

namespace graft
{

#define GRAFT_STATUS_LIST(EXP) \
    EXP(None) \
    EXP(Ok) \
    EXP(Forward) \
    EXP(Again) \
    EXP(Error) \
    EXP(Drop) \
    EXP(Busy) \
    EXP(InternalError) \
    EXP(Postpone) \
    EXP(Stop) //for timer events

#define EXP_TO_ENUM(x) x,
#define EXP_TO_STR(x) #x,

enum class Status : int { GRAFT_STATUS_LIST(EXP_TO_ENUM) };

enum class WsEvent : int
{
    EV_NewInConn, //"host:port"
    EV_OutConnOk, //"host:port/uri"
    EV_OutConnFail, //"host:port/uri", error
    EV_Frame, //"host:port[/uri]", data
    EV_FrameSentOk, //"host:port[/uri]"
    EV_FrameSentError, //"host:port[/uri]", error
    EV_ConnClosed, //"host:port[/uri]"
    EV_ConnClosedOk, //"host:port[/uri]"
    EV_ConnClosedError, //"host:port[/uri]", error
};

enum class WsCommand : int
{
    CMD_Connect, //"host:port"
    CMD_Send, //"host:port", data
    CMD_CloseConn, //"host:port"
};

using Addr = std::string;
using WsFrame = std::string;

using WsInDeque = std::deque<std::tuple<WsEvent,Addr,WsFrame>>;
using WsOutDeque = std::deque<std::tuple<WsCommand,Addr,WsFrame>>;

}//namespace graft
