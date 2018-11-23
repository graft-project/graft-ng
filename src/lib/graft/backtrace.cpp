
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
#include <cxxabi.h>

#include "lib/graft/backtrace.h"

#define TRACE_SIZE_MAX 32
#define FUNC_NAME_SIZE_MAX 256

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

    char *funcname =  (char*) malloc(FUNC_NAME_SIZE_MAX);
    int i, trace_size = backtrace(trace, TRACE_SIZE_MAX);

    if (trace_size == 0)
    {
        fprintf(stderr, "\t<stack trace is possibly corrupt>\n");
        return;
    }

    messages = backtrace_symbols(trace, trace_size);
    for (i = 1; i < trace_size && messages; ++i)
    {
        char *begin_name = NULL, *begin_offset = NULL, *end_offset = NULL, *p;
	for (p = messages[i]; *p; ++p)
	{
	    if (*p == '(')
		begin_name = p;
	    else if (*p == '+')
		begin_offset = p;
	    else if (*p == ')' && begin_offset) {
		end_offset = p;
		break;
	    }
	}
	if (begin_name && begin_offset && end_offset && begin_name < begin_offset)
	{
	    int status;
            size_t funcnamesize = FUNC_NAME_SIZE_MAX;

	    *begin_name++ = '\0';
	    *begin_offset++ = '\0';
	    *end_offset = '\0';

	    p = abi::__cxa_demangle(begin_name, funcname, &funcnamesize, &status);
	    if (status == 0)
            {
		funcname = p;
		fprintf(stderr, "\t#%d %s : %s+%s\n", i, messages[i], funcname, begin_offset);
	    }
	    else
		fprintf(stderr, "\t#%d %s : %s()+%s\n", i, messages[i], begin_name, begin_offset);
	}
	else
	    fprintf(stderr, "\t#%d %s\n", i, messages[i]);
    }
    free(messages);
    free(funcname);
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

