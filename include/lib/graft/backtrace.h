#pragma once

#include <signal.h>

#ifdef __cplusplus
extern "C" {
#endif
    void graft_bt();
    void graft_bt_sighandler(int sig, siginfo_t *info, void *ucontext);
#ifdef __cplusplus
}
#endif

