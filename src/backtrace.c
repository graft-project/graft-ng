#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#ifndef __USE_GNU
#define __USE_GNU
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <execinfo.h>
#include <ucontext.h>

#include "backtrace.h"

#define TRACE_SIZE_MAX 16

/* This structure mirrors the one found in /usr/include/asm/ucontext.h */
typedef struct {
    unsigned long     uc_flags;
    struct ucontext   *uc_link;
    stack_t           uc_stack;
    struct sigcontext uc_mcontext;
    sigset_t          uc_sigmask;
} sig_ucontext_t;

void graft_bt()
{
    void *trace[TRACE_SIZE_MAX];
    char **messages = (char **) NULL;
    int i, trace_size = 0;

    trace_size = backtrace(trace, TRACE_SIZE_MAX);

    messages = backtrace_symbols(trace, trace_size);
    for (i = 1; i < trace_size && messages; ++i)
        fprintf(stderr, "\t#%d %s\n", i, messages[i]);

    free(messages);
}

void graft_bt_sighandler(int sig, siginfo_t *info, void *ucontext)
{
    void *caller_address;
    sig_ucontext_t *ctx = (sig_ucontext_t *)ucontext;

#if defined(__i386__)
    caller_address = (void *) ctx->uc_mcontext.eip;
#elif defined(__x86_64__)
    caller_address = (void *) ctx->uc_mcontext.rip;
#else
#error Unsupported architecture
#endif
    fprintf(stderr, "Signal %d (%s), address is %p from %p\n", 
            sig, strsignal(sig), info->si_addr, (void *)caller_address);

    graft_bt();

    exit(EXIT_FAILURE);
}

