
#pragma once

#include "lib/graft/task.h"

namespace graft
{

class StateMachine final
{
public:

#define GRAFT_STATE_LIST(EXP) \
    EXP(EXECUTE) \
    EXP(PRE_ACTION) \
    EXP(CHK_PRE_ACTION) \
    EXP(WORKER_ACTION) \
    EXP(CHK_WORKER_ACTION) \
    EXP(WORKER_ACTION_DONE) \
    EXP(POST_ACTION) \
    EXP(CHK_POST_ACTION) \
    EXP(AGAIN) \
    EXP(EXIT) \

    enum State { GRAFT_STATE_LIST(EXP_TO_ENUM) };

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

    void process(BaseTaskPtr bt);

    using H3 = Router::Handler3;

    const Guard has(Router::Handler H3::* act);
    const Guard hasnt(Router::Handler H3::* act);

    State m_state;
    enum columns          { smStateStart,   smStatuses, smStateEnd, smGuard,    smAction};
    using row = std::tuple< State,          Statuses,   State,      Guard,      Action  >;
    std::vector<row> m_table;
};

}//namespace graft

