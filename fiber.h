#ifndef FIBER_H
#define FIBER_H

#include <stddef.h>
#include <stdint.h>

#ifndef FIBER_SHARED
#  define FIBER_SHARED __attribute__((visibility("default")))
#endif

typedef uint32_t FiberState;

#define FS_EXECUTING ((FiberState) 1)
#define FS_TOPLEVEL ((FiberState) 2)
#define FS_ALIVE ((FiberState) 4)

#if defined(FIBER_BITS32)
typedef struct {
    void *sp;
    void *rbp;
} Regs;
#elif defined FIBER_BITS64
typedef struct {
    void *sp;
    void *rbp;
    void *rbx;
    void *r12;
    void *r13;
    void *r14;
    void *r15;
} Regs;
#else
#  error "FIBER_BITS{32,64} not defined"
#endif

typedef struct {
    size_t stack_size;
    void *stack;
    FiberState state;
    Regs regs;
} Fiber;

typedef void (*FiberFunc)(void *);

FIBER_SHARED Fiber *fiber_init(Fiber *fiber, void *stack, size_t stack_size);

FIBER_SHARED void fiber_init_toplevel(Fiber *fiber);

FIBER_SHARED void fiber_alloc(Fiber **fiber, size_t stack_size);

FIBER_SHARED void fiber_free(Fiber *fiber);

FIBER_SHARED void fiber_switch(Fiber *from, Fiber *to);

FIBER_SHARED void fiber_push_return(Fiber *fiber, FiberFunc f, const void *args, size_t args_size);

FIBER_SHARED void fiber_reserve_return(Fiber *fiber, FiberFunc f, void **args_dest, size_t args_size);

FIBER_SHARED void fiber_exec_on(Fiber *active, Fiber *temp, FiberFunc f, void *args, size_t args_size);

static inline int fiber_is_toplevel(Fiber *fiber) {
    return (fiber->state & FS_TOPLEVEL) != 0;
}

static inline int fiber_is_executing(Fiber *fiber) {
    return (fiber->state & FS_EXECUTING) != 0;
}

static inline int fiber_is_alive(Fiber *fiber) {
    return (fiber->state & FS_ALIVE) != 0;
}

#endif
