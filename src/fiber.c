#ifndef FIBER_AMALGAMATED
#    include <fiber/fiber.h>

#    include "fiber_asm.h"

#    include <hu/annotations.h>
#endif

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if HU_OS_POSIX_P
#    include <sys/mman.h>
#    include <unistd.h> // getpagesize()
#    define GETPAGESIZE getpagesize
#    if HU_C_11_P
#        define ALIGNED_ALLOC aligned_alloc
#    elif HU_OS_BSD_P || defined(_POSIX_C_SOURCE) && _POSIX_C_SOURCE >= 200112L
#        define USE_POSIX_MEMALIGN 1
#    else
#        include <malloc.h>
#        define ALIGNED_ALLOC                                                  \
            memalign /* we assume that we are able to release the memory using \
                        free() */
#    endif
#    define ALIGNED_FREE free
#elif HU_OS_WINDOWS_P
#    define WIN32_LEAN_AND_MEAN 1
#    define VC_EXTRALEAN 1
#    define NOMINMAX 1
#    define NOGDI 1
#    include <Windows.h>
#    define ALIGNED_ALLOC(algn, sz) _aligned_malloc((sz), (algn))
#    define ALIGNED_FREE _aligned_free
#    define GETPAGESIZE win32_get_pagesize
#else
#    error "Platform not supported"
#endif

#ifndef FIBER_STACK_ALIGNMENT
static const size_t STACK_ALIGNMENT = FIBER_DEFAULT_STACK_ALIGNMENT;
#else
static const size_t STACK_ALIGNMENT = FIBER_STACK_ALIGNMENT;
#endif

static const size_t ARG_ALIGNMENT = 8;
static const size_t WORD_SIZE = sizeof(void *);

#if HU_HAVE_NONNULL_PARAMS_P || HU_HAVE_INOUT_NONNULL_P
#    define NULL_CHECK(arg, msg)
#else
#    define NULL_CHECK(arg, msg) assert(arg &&msg)
#endif

#define error_abort(msg)                                                       \
    do {                                                                       \
        fprintf(stderr, "%s\n", msg);                                          \
        abort();                                                               \
    } while (0)

static inline char *
stack_align_n(char *sp, size_t n)
{
    return (char *) ((uintptr_t) sp & ~(uintptr_t)(n - 1));
}

HU_MAYBE_UNUSED
bool
is_stack_aligned(void *sp)
{
    return ((uintptr_t) sp & (STACK_ALIGNMENT - 1)) == 0;
}

static inline void
push(char **sp, void *val)
{
    *sp -= WORD_SIZE;
    *(void **) *sp = val;
}

static inline void
push_rel(char **sp, intptr_t rel)
{
    *sp -= WORD_SIZE;
    *(intptr_t *) *sp = rel;
}

typedef struct
{
    Fiber *fiber;
    FiberCleanupFunc cleanup;
    void *arg;
} FiberGuardArgs;

HU_NORETURN
static void
fiber_guard(void *fbr);

static intptr_t
sprel(const Fiber *fbr, void *sp)
{
    return (intptr_t) sp;
}

static char *
spabs(const Fiber *fbr, intptr_t rel)
{
    return (char *) (void *) rel;
}

static void
fiber_init_(Fiber *fbr, FiberCleanupFunc cleanup, void *arg)
{
    memset(&fbr->_regs, 0, sizeof fbr->_regs);
    uintptr_t sp =
      (uintptr_t)((char *) fbr->_stack + fbr->_stack_size - WORD_SIZE);
    sp &= ~(STACK_ALIGNMENT - 1);
    fbr->_regs.sp = (void *) sp;
    FiberGuardArgs *args;
    fiber_reserve_return(fbr, fiber_guard, (void **) &args, sizeof *args);
    args->fiber = fbr;
    args->cleanup = cleanup;
    args->arg = arg;
    fbr->_state |= FIBER_FS_ALIVE;
}

Fiber *
fiber_init(Fiber *fbr,
           void *stack,
           size_t stack_size,
           FiberCleanupFunc cleanup,
           void *arg)
{
    NULL_CHECK(fbr, "Fiber cannot be NULL");
    fbr->_stack = stack;
    fbr->_stack_size = stack_size;
    fbr->_alloc_stack = NULL;
    fbr->_state = 0;
    fiber_init_(fbr, cleanup, arg);
    return fbr;
}

void
fiber_init_toplevel(Fiber *fbr)
{
    NULL_CHECK(fbr, "Fiber cannot be NULL");
    fbr->_stack = NULL;
    fbr->_stack_size = (size_t) -1;
    fbr->_alloc_stack = NULL;
    memset(&fbr->_regs, 0, sizeof fbr->_regs);
    fbr->_state = FIBER_FS_ALIVE | FIBER_FS_TOPLEVEL | FIBER_FS_EXECUTING;
}

static void *
alloc_aligned_chunks(size_t nchunks, size_t align)
{
    size_t sz = nchunks * align;
#ifdef ALIGNED_ALLOC
    return ALIGNED_ALLOC(align, sz);
#elif defined(USE_POSIX_MEMALIGN)
    void *ret;
    if (posix_memalign(&ret, align, sz) != 0)
        return NULL;
    return ret;
#endif
}

static void
free_pages(void *p)
{
    ALIGNED_FREE(p);
}

HU_CONST_FN
static size_t
get_page_size()
{
    static size_t PAGE_SIZE = 0;
    size_t pgsz = PAGE_SIZE;
    if (hu_likely(pgsz != 0))
        return pgsz;

#if HU_OS_POSIX_P
    pgsz = (size_t) getpagesize();
#elif HU_OS_WINDOWS_P
    SYSTEM_INFO sysnfo;
    GetSystemInfo(&sysnfo);
    pgsz = (size_t) sysnfo.dwPageSize;
#endif
    PAGE_SIZE = pgsz;
    return pgsz;
}

static bool
protect_page(void *p, bool rw)
{
#if HU_OS_POSIX_P
    return mprotect(
             p, get_page_size(), rw ? PROT_READ | PROT_WRITE : PROT_NONE) == 0;
#elif HU_OS_WINDOWS_P
    DWORD old_protect;
    return VirtualProtect(p,
                          get_page_size(),
                          rw ? PAGE_READWRITE : PAGE_NOACCESS,
                          &old_protect) != 0;
#else
#    error "BUG: platform not properly handled"
#endif
}

bool
fiber_alloc(Fiber *fbr,
            size_t size,
            FiberCleanupFunc cleanup,
            void *arg,
            FiberFlags flags)
{
    NULL_CHECK(fbr, "Fiber cannot be NULL");
    flags &= FIBER_FLAG_GUARD_LO | FIBER_FLAG_GUARD_HI;
    fbr->_stack_size = size;
    const size_t stack_size = size;

    if (!flags) {
        fbr->_alloc_stack = fbr->_stack = malloc(stack_size);
        if (!fbr->_alloc_stack)
            return false;
    } else {
        size_t pgsz = get_page_size();
        size_t npages = (size + pgsz - 1) / pgsz;
        if (flags & FIBER_FLAG_GUARD_LO)
            ++npages;
        if (flags & FIBER_FLAG_GUARD_HI)
            ++npages;
        fbr->_alloc_stack = alloc_aligned_chunks(npages, pgsz);
        if (hu_unlikely(!fbr->_alloc_stack))
            return false;

        if (flags & FIBER_FLAG_GUARD_LO)
            if (hu_unlikely(!protect_page(fbr->_alloc_stack, false)))
                goto fail;

        if (flags & FIBER_FLAG_GUARD_HI)
            if (hu_unlikely(!protect_page(
                  (char *) fbr->_alloc_stack + (npages - 1) * pgsz, false)))
                goto fail;
        if (flags & FIBER_FLAG_GUARD_LO)
            fbr->_stack = (char *) fbr->_alloc_stack + pgsz;
        else
            fbr->_stack = fbr->_alloc_stack;
    }

    fbr->_state = flags;
    fiber_init_(fbr, cleanup, arg);
    return true;

fail:
    free_pages(fbr->_alloc_stack);
    return false;
}

void
fiber_destroy(Fiber *fbr)
{
    assert(!fiber_is_executing(fbr));
    assert(!fiber_is_toplevel(fbr));

    if (!fbr->_alloc_stack)
        return;

    if (fbr->_state &
        (FIBER_FS_HAS_HI_GUARD_PAGE | FIBER_FS_HAS_LO_GUARD_PAGE)) {
        size_t pgsz = get_page_size();
        size_t npages = (fbr->_stack_size + pgsz - 1) / pgsz;
        if (fbr->_state & FIBER_FS_HAS_LO_GUARD_PAGE) {
            ++npages;
            protect_page(fbr->_alloc_stack, true);
        }

        if (fbr->_state & FIBER_FS_HAS_HI_GUARD_PAGE) {
            protect_page((char *) fbr->_alloc_stack + npages * pgsz, true);
        }

        free_pages(fbr->_alloc_stack);
    } else {
        free(fbr->_alloc_stack);
    }

    fbr->_stack = NULL;
    fbr->_stack_size = 0;
    fbr->_regs.sp = 0;
    fbr->_alloc_stack = NULL;
}

void
fiber_switch(Fiber *from, Fiber *to)
{
    NULL_CHECK(from, "Fiber cannot be NULL");
    NULL_CHECK(to, "Fiber cannot be NULL");

    if (from == to)
        return;

    assert(fiber_is_executing(from));
    assert(!fiber_is_executing(to));
    assert(fiber_is_alive(to));
    from->_state &= ~FIBER_FS_EXECUTING;
    to->_state |= FIBER_FS_EXECUTING;
    fiber_asm_switch(&from->_regs, &to->_regs);
}

#if hu_has_attribute(weak)
#    define HAVE_probe_stack_weak_dummy
__attribute__((weak)) void
_probe_stack_weak_dummy(volatile char *sp, size_t sz);

__attribute__((weak)) void
_probe_stack_weak_dummy(volatile char *sp, size_t sz)
{
    (void) sp;
    (void) sz;
}
#endif

HU_NOINLINE
static void
probe_stack(volatile char *sp0, size_t sz, size_t pgsz)
{
    volatile char *sp = sp0;
#if HU_COMP_GNUC_P
    __asm__ __volatile__("" : : "r"(sp) : "memory");
#endif
    size_t i = 0;
    while (i < sz) {
        *(volatile uintptr_t *) sp |= (uintptr_t) 0;
        i += pgsz;
        sp -= pgsz;
    }

#ifdef HAVE_probe_stack_weak_dummy
    _probe_stack_weak_dummy(sp0, sz);
#endif
}

void
fiber_reserve_return(Fiber *fbr,
                     FiberFunc f,
                     void **args_dest,
                     size_t args_size)
{
    NULL_CHECK(fbr, "Fiber cannot be NULL");
    assert(!fiber_is_executing(fbr));

    char *sp = spabs(fbr, fbr->_regs.sp);
    size_t arg_align =
      ARG_ALIGNMENT > STACK_ALIGNMENT ? ARG_ALIGNMENT : STACK_ALIGNMENT;
    sp = stack_align_n(sp - args_size, arg_align);
    *args_dest = sp;

    size_t pgsz = get_page_size();
    if (hu_unlikely(args_size > pgsz - 100))
        probe_stack(sp, args_size, pgsz);

    assert(is_stack_aligned(sp));

    push(&sp, fbr->_regs.lr);
    push_rel(&sp, fbr->_regs.sp);
    push(&sp, hu_cxx_reinterpret_cast(void *, f));
    push(&sp, *args_dest);

    assert(is_stack_aligned(sp));

    fbr->_regs.lr = hu_cxx_reinterpret_cast(void *, fiber_asm_invoke);

    fbr->_regs.sp = sprel(fbr, sp);
}

void
fiber_exec_on(Fiber *active, Fiber *temp, FiberFunc f, void *args)
{
    NULL_CHECK(active, "Fiber cannot be NULL");
    NULL_CHECK(temp, "Fiber cannot be NULL");
    assert(fiber_is_executing(active));

    if (active == temp) {
        f(args);
    } else {
        assert(!fiber_is_executing(temp));
        temp->_state |= FIBER_FS_EXECUTING;
        active->_state &= ~FIBER_FS_EXECUTING;
        fiber_asm_exec_on_stack(args, f, spabs(temp, temp->_regs.sp));
        active->_state |= FIBER_FS_EXECUTING;
        temp->_state &= ~FIBER_FS_EXECUTING;
    }
}

static void
fiber_guard(void *argsp)
{
    FiberGuardArgs *args = (FiberGuardArgs *) argsp;
    args->fiber->_state &= ~FIBER_FS_ALIVE;
    args->cleanup(args->fiber, args->arg);
    error_abort("ERROR: fiber cleanup returned");
}

#ifdef FIBER_ASM_CHECK_ALIGNMENT
HU_NORETURN
HU_DSO_HIDDEN
void
fiber_align_check_failed(void)
{
    error_abort("ERROR: fiber stack alignment check failed");
}
#endif
