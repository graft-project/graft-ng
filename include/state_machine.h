#pragma once

#include "task.h"

namespace graft
{

class StateMachine final
{
public:
    enum State
    {
        EXECUTE,
        PRE_ACTION,
        CHK_PRE_ACTION,
        WORKER_ACTION,
        CHK_WORKER_ACTION,
        WORKER_ACTION_DONE,
        POST_ACTION,
        CHK_POST_ACTION,
        AGAIN,
        EXIT,
    };

    using St = graft::Status;
    using Statuses = std::initializer_list<graft::Status>;
    using Guard = std::function<bool (BaseTaskPtr bt)>;
    using Action = std::function<void (BaseTaskPtr bt)>;

    StateMachine(State initial_state = EXECUTE)
    {
        state(initial_state);
        init_table();
    }

    void dispatch(BaseTaskPtr bt, State initial_state)
    {
//        state(State(initial_state));
        state(initial_state);
        while(state() != EXIT)
        {
            process(bt);
        }
    }

private:
    void init_table();
    State state() const { return m_state; }
    State state(State state) { return m_state = state; }
    St status(BaseTaskPtr bt) const { return bt->getLastStatus(); }

    void process(BaseTaskPtr bt)
    {
        St cur_stat = status(bt);

        for(auto& r : m_table)
        {
            if(m_state != std::get<0>(r)) continue;

            Statuses& ss = std::get<1>(r);
            if(ss.size()!=0)
            {
                bool res = false;
                for(auto s : ss)
                {
                    if(s == cur_stat)
                    {
                        res = true;
                        break;
                    }
                }
                if(!res) continue;
            }

            Guard& g = std::get<3>(r);
            if(g && !g(bt)) continue;

            Action& a = std::get<4>(r);
            if(a) a(bt);
            m_state = std::get<2>(r);
            return;
        }
        throw std::runtime_error("State machine table is not complete");
    }
private:

    using H3 = Router::Handler3;

    const Guard has(Router::Handler H3::* act);
    const Guard hasnt(Router::Handler H3::* act);

    State m_state;
    using row = std::tuple<State, Statuses, State, Guard, Action>;
    std::vector<row> m_table;
};

}//namespace graft

