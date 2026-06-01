/*
 * fiber_compat.c — POSIX (ucontext) implementation of the Win32 Fiber API
 * used by the MMX task scheduler. See fiber_compat.h. Compiled to nothing on
 * Windows, where the native Fiber API is used directly.
 */
#ifndef _WIN32
#define _XOPEN_SOURCE 600   /* expose ucontext on macOS/glibc — must precede includes */

#include "fiber_compat.h"
#include <ucontext.h>
#include <stdlib.h>

typedef struct Fiber {
    ucontext_t ctx;
    FiberProc  entry;
    void      *param;
    void      *stack;     /* NULL for a ConvertThreadToFiber handle */
} Fiber;

/* The fiber currently executing on this thread. Updated by SwitchToFiber
 * before the context swap, so a freshly-started fiber's trampoline sees
 * itself here. */
static Fiber *g_current_fiber = NULL;

static void fiber_trampoline(void) {
    Fiber *self = g_current_fiber;
    if (self && self->entry)
        self->entry(self->param);
    /* MMX fiber entries never fall off the end (they loop SwitchToFiber back
     * to the scheduler). If one ever does, there is no safe continuation. */
    abort();
}

void *ConvertThreadToFiber(void *param) {
    (void)param;
    Fiber *f = (Fiber *)calloc(1, sizeof(Fiber));
    if (!f) return NULL;
    /* No stack of its own — it rides the thread's existing stack. The ctx is
     * populated by the first swapcontext that switches away from it. */
    g_current_fiber = f;
    return f;
}

void *CreateFiber(size_t stack_size, FiberProc entry, void *param) {
    Fiber *f = (Fiber *)calloc(1, sizeof(Fiber));
    if (!f) return NULL;
    f->stack = malloc(stack_size);
    if (!f->stack) { free(f); return NULL; }
    f->entry = entry;
    f->param = param;
    if (getcontext(&f->ctx) != 0) { free(f->stack); free(f); return NULL; }
    f->ctx.uc_stack.ss_sp   = f->stack;
    f->ctx.uc_stack.ss_size = stack_size;
    f->ctx.uc_link          = NULL;
    makecontext(&f->ctx, fiber_trampoline, 0);
    return f;
}

void SwitchToFiber(void *fiber) {
    Fiber *target = (Fiber *)fiber;
    if (!target || target == g_current_fiber) return;
    Fiber *prev = g_current_fiber;
    g_current_fiber = target;
    swapcontext(&prev->ctx, &target->ctx);
}

void DeleteFiber(void *fiber) {
    Fiber *f = (Fiber *)fiber;
    if (!f) return;
    free(f->stack);   /* free(NULL) is fine for thread-origin fibers */
    free(f);
}

unsigned long GetLastError(void) { return 0; }

#endif /* !_WIN32 */
