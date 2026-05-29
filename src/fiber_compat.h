#pragma once
/*
 * fiber_compat.h — cross-platform fiber API for the MMX cooperative task
 * scheduler (mmx_rtl.c).
 *
 * The scheduler cooperatively switches between a "scheduler" fiber and one
 * fiber per task slot, each with its own C stack so a task can yield mid-call
 * and resume later. On Windows this is the native Fiber API. On POSIX there is
 * no Fiber API, so we provide an equivalent backed by ucontext_t — same five
 * entry points, same semantics — so mmx_rtl.c is identical on both platforms.
 */
#ifdef _WIN32
#  include <windows.h>
#else
#  include <stddef.h>

#  ifndef CALLBACK
#    define CALLBACK   /* no calling-convention decoration on POSIX */
#  endif

typedef void (*FiberProc)(void *);

/* Promote the current thread to a fiber; returns its handle (or NULL). */
void *ConvertThreadToFiber(void *param);

/* Create a fiber with its own stack that will run entry(param) when first
 * switched to. Returns a handle (or NULL on failure). */
void *CreateFiber(size_t stack_size, FiberProc entry, void *param);

/* Cooperatively switch execution to the given fiber. */
void  SwitchToFiber(void *fiber);

/* Free a fiber created by CreateFiber. */
void  DeleteFiber(void *fiber);

/* Win32 parity for the scheduler's error logging; always 0 on POSIX. */
unsigned long GetLastError(void);
#endif
