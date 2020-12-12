#ifndef FIBER_MACH_H
#define FIBER_MACH_H

#ifndef FIBER_AMALGAMATED
#    include <hu/arch.h>
#    include <hu/bits.h>
#    include <hu/endian.h>
#    include <hu/objfmt.h>
#    include <hu/os.h>
#endif

/* #ifndef FIBER_H */
/* #    error "include fiber.h before fiber_mach.h" */
/* #endif */

#if HU_ARCH_X86_P && HU_BITS_32_P && HU_OS_POSIX_P
#    define FIBER_TARGET_X86_CDECL 1
#    define FIBER_DEFAULT_STACK_ALIGNMENT 16
typedef struct
{
    intptr_t sp;
    void *lr;
    char *ebp;
    char *ebx;
    char *edi;
    char *esi;
} FiberRegs;
#elif HU_ARCH_X86_P && HU_BITS_64_P && HU_OS_POSIX_P
#    define FIBER_TARGET_AMD64_SYSV
#    define FIBER_STACK_ALIGNMENT 16
typedef struct
{
    char *base;
    intptr_t sp;
    void *lr;
    char *rbp;
    char *rbx;
    char *r12;
    char *r13;
    char *r14;
    char *r15;
} FiberRegs;
#elif HU_ARCH_X86_P && HU_BITS_64_P && HU_OS_WINDOWS_P
#    define FIBER_TARGET_AMD64_WIN64 1
#    define FIBER_DEFAULT_STACK_ALIGNMENT 16
typedef struct
{
    intptr_t sp;
    void *lr;
    char *rbx;
    char *rbp;
    char *rdi;
    char *rsi;
    char *r12;
    char *r13;
    char *r14;
    char *r15;
    /* 10 * 16 bytes, add aditional 8 bytes to make 16byte alignment possible */
    double xmm[21];
} FiberRegs;
#elif HU_ARCH_X86_P && HU_BITS_32_P && HU_OS_WINDOWS_P
#    define FIBER_TARGET_X86_WIN32 1
#    define FIBER_CCONV __cdecl
#    define FIBER_DEFAULT_STACK_ALIGNMENT 4
typedef struct
{
    intptr_t sp;
    void *lr;
    char *ebx;
    char *ebp;
    char *esi;
    char *edi;
} FiberRegs;
#elif HU_ARCH_ARM_P && HU_BITS_32_P && HU_OS_POSIX_P
#    define FIBER_TARGET_ARM32_EABI
#    define FIBER_DEFAULT_STACK_ALIGNMENT 8
typedef struct
{
    char *__pad;
    char *r[9];  /* r4 - r12 */
    intptr_t sp; /* r13 */
    void *lr;    /* r14 */
    double d[8]; /* d8 - d15 */
} FiberRegs;
#elif HU_ARCH_ARM_P && HU_BITS_64_P && HU_OS_POSIX_P
#    define FIBER_TARGET_AARCH64_APCS 1
#    define FIBER_DEFAULT_STACK_ALIGNMENT 16
typedef struct
{
    intptr_t sp;
    void *lr;    /* r30 */
    char *fp;    /* r29 */
    char *r[10]; /* r19 - r28 */
    double d[8]; /* d8 - d15 */
} FiberRegs;
#elif HU_ARCH_RISCV_P && HU_BITS_64_P && HU_OS_POSIX_P
#    define FIBER_TARGET_RISCV64_ELF 1
#    define FIBER_DEFAULT_STACK_ALIGNMENT 16
#    ifndef __riscv_float_abi_double
#        error "this RISCV abi is not supported use -mabi=lp64d"
#    endif
typedef struct
{
    intptr_t sp;
    void *lr;
    char *s[12];
    double fs[12];
} FiberRegs;
#elif HU_ARCH_PPC_P && HU_BITS_64_P && HU_LITTLE_ENDIAN_P && HU_OBJFMT_ELF_P
#    define FIBER_TARGET_PPC64LE_ELF 1
#    define FIBER_DEFAULT_STACK_ALIGNMENT 16
#    if !defined(_CALL_ELF) || _CALL_ELF != 2
#        error "this PowerPC ABI is not supported, use -mabi=elfv2"
#    endif
typedef struct
{
    uint32_t cr;
    uint32_t vrsave;
    void *lr;              /* r0 */
    intptr_t sp;           /* r1 */
    char *r[18];           /* r14 - r31 */
    double *f[18];         /* f14 - f31 */
    double *v[12 * 2 + 1]; /* v20 - v31, adjusted to be 16 byte aligned */
} FiberRegs;
#else
#    error "fiber: system/architecture target not supported"
#endif

#ifndef FIBER_CCONV
#    define FIBER_CCONV
#endif

#endif
