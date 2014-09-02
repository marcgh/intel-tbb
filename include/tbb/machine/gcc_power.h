/*
    Copyright 2005-2013 Intel Corporation.  All Rights Reserved.

    This file is part of Threading Building Blocks.

    Threading Building Blocks is free software; you can redistribute it
    and/or modify it under the terms of the GNU General Public License
    version 2 as published by the Free Software Foundation.

    Threading Building Blocks is distributed in the hope that it will be
    useful, but WITHOUT ANY WARRANTY; without even the implied warranty
    of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Threading Building Blocks; if not, write to the Free Software
    Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

    As a special exception, you may use this file as part of a free software
    library without restriction.  Specifically, if other files instantiate
    templates or use macros or inline functions from this file, or you compile
    this file and link it with other files to produce an executable, this
    file does not by itself cause the resulting executable to be covered by
    the GNU General Public License.  This exception does not however
    invalidate any other reasons why the executable file might be covered by
    the GNU General Public License.
*/

#if !defined(__TBB_machine_H) || defined(__TBB_machine_gcc_power_H)
#error Do not #include this internal file directly; use public TBB headers instead.
#endif

#define __TBB_machine_gcc_power_H

#include <stdint.h>
#include <unistd.h>

// This file is for Power Architecture with compilers supporting GNU inline-assembler syntax
// (currently GNU g++ and IBM XL).
// Note that XL V9.0 (sometimes?) has trouble dealing with empty input and/or clobber lists,
// so they should be avoided.

#if __powerpc64__ || __ppc64__
    // IBM XL documents __powerpc64__ (and __PPC64__).
    // Apple GCC documents __ppc64__ (with __ppc__ only on 32-bit).
    // GNU GCC (standard one, as well as Advance Toolchain) documents __powerpc64__ (and __PPC64__).
    #define __TBB_WORDSIZE 8
#else
    #define __TBB_WORDSIZE 4
#endif

// Traditionally Power Architecture is big-endian.
// Little-endian could be just an address manipulation (compatibility with TBB not verified),
// or normal little-endian (on more recent systems). Embedded PowerPC systems may support
// page-specific endianness, but then one endianness must be hidden from TBB so that it still sees only one.
#if __BIG_ENDIAN__ || (defined(__BYTE_ORDER__) && __BYTE_ORDER__==__ORDER_BIG_ENDIAN__)
    #define __TBB_ENDIANNESS __TBB_ENDIAN_BIG
#elif __LITTLE_ENDIAN__ || (defined(__BYTE_ORDER__) && __BYTE_ORDER__==__ORDER_LITTLE_ENDIAN__)
    #define __TBB_ENDIANNESS __TBB_ENDIAN_LITTLE
#elif defined(__BYTE_ORDER__)
    #define __TBB_ENDIANNESS __TBB_ENDIAN_UNSUPPORTED
#else
    #define __TBB_ENDIANNESS __TBB_ENDIAN_DETECT
#endif

// On Power Architecture, (lock-free) 64-bit atomics require 64-bit hardware:
#if __TBB_WORDSIZE==8
    // Do not change the following definition, because TBB itself will use 64-bit atomics in 64-bit builds.
    #define __TBB_64BIT_ATOMICS 1
#elif __bgp__
    // Do not change the following definition, because this is known 32-bit hardware.
    #define __TBB_64BIT_ATOMICS 0
#else
    // To enable 64-bit atomics in 32-bit builds, set the value below to 1 instead of 0.
    // You must make certain that the program will only use them on actual 64-bit hardware
    // (which typically means that the entire program is only executed on such hardware),
    // because their implementation involves machine instructions that are illegal elsewhere.
    // The setting can be chosen independently per compilation unit,
    // which also means that TBB itself does not need to be rebuilt.
    // Alternatively (but only for the current architecture and TBB version),
    // override the default as a predefined macro when invoking the compiler.
    #ifndef __TBB_64BIT_ATOMICS
        #define __TBB_64BIT_ATOMICS 0
    #endif
#endif

// Save original compiler diagnostic options and disable a problematic one
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-qual"

inline uint8_t __TBB_machine_cmpswp1(volatile void *ptr, uint8_t value, uint8_t comparand) {
    uint8_t result;
    #if __TBB_ENDIANNESS==__TBB_ENDIAN_BIG
    uint8_t temporary;
    int offset = (long int)ptr & 0x3,
    maskoff = (offset == 0) ? 0x00ffffff :
              (offset == 1) ? 0xff00ffff :
              (offset == 2) ? 0xffff00ff : 0xffffff00;

    __asm__ __volatile__("0:\n\t"
                         "sync\n\t"                             /* ==> retry loop */
                         "lwarx %[res], 0, %[ptr]\n\t"          /* load w/ reservation */
                         "mr %[tmp], %[res]\n\t"                /* keep a copy for later use */
                         "cmpwi %[off], 0\n\t"                  /* if BE offset is 0 (shift 24) */
                         "beq 2f\n\t"                           /* go to shift24 */
                         "cmpwi %[off], 1\n\t"                  /* if BE offset is 1 (shift 16) */
                         "beq 3f\n\t"                           /* go to shift16 */
                         "cmpwi %[off], 2\n\t"                  /* if BE offset is 2 (shift 8) */
                         "beq 4f\n"                             /* go to shift8 */

                         "1:\n\t"                               /* ==> Rightmost byte to work on (noshift) */
                         "andi. %[res], %[res], 0xff\n\t"       /* clean up retrieved value (no shift needed) */
                         "cmpw %[cmp], %[res]\n\t"              /* compare against comparand (** dup **) */
                         "bne- 6f\n\t"                          /* exit if not same (** dup **) */
                         "and %[tmp], %[tmp], %[msk]\n\t"       /* set the mask on saved value */
                         "or %[val], %[val], %[tmp]\n\t"        /* combine original value with new one for storage */
                         "b 5f\n"                               /* step right into storage */

                         "2:\n\t"                               /* ==> Leftmost byte to work on (shift24) */
                         "srwi %[res], %[res], 24\n\t"          /* perform required shift */
                         "cmpw %[cmp], %[res]\n\t"              /* compare against comparand (** dup **) */
                         "bne- 6f\n\t"                          /* exit if not same (** dup **) */
                         "slwi %[val], %[val], 24\n\t"          /* rotate new value to proper position in word */
                         "and %[tmp], %[tmp], %[msk]\n\t"       /* set the mask on saved value */
                         "or %[val], %[val], %[tmp]\n\t"        /* combine original value with new one for storage */
                         "b 5f\n"                               /* step right into storage */

                         "3:\n\t"                               /* ==> Middle left byte to work on (shift16) */
                         "andc %[res], %[res], %[msk]\n\t"      /* clean everything except requested byte value */
                         "srwi %[res], %[res], 16\n\t"          /* otherwise perform required shift */
                         "cmpw %[cmp], %[res]\n\t"              /* compare against comparand (** dup **) */
                         "bne- 6f\n\t"                          /* exit if not same (** dup **) */
                         "slwi %[val], %[val], 16\n\t"          /* rotate new value to proper position in word */
                         "and %[tmp], %[tmp], %[msk]\n\t"       /* set the mask on saved value */
                         "or %[val], %[val], %[tmp]\n\t"        /* combine original value with new one for storage */
                         "b 5f\n"                               /* step right into storage */

                         "4:\n\t"                               /* ==> Middle right byte to work on (shift8) */
                         "andc %[res], %[res], %[msk]\n\t"      /* clean everything except requested byte value */
                         "srwi %[res], %[res], 8\n\t"           /* otherwise perform required shift */
                         "cmpw %[cmp], %[res]\n\t"              /* compare against comparand (** dup **) */
                         "bne- 6f\n\t"                          /* exit if not same (** dup **) */
                         "slwi %[val], %[val], 8\n\t"           /* rotate new value to proper position in word */
                         "and %[tmp], %[tmp], %[msk]\n\t"       /* set the mask on saved value */
                         "or %[val], %[val], %[tmp]\n"          /* combine original value with new one for storage */

                         "5:\n\t"                               /* ==> Storage steps (storage8) */
                         "stwcx. %[val], 0, %[ptr]\n\t"         /* store new value */
                         "bne- 0b\n"                            /* retry if reservation lost */

                         "6:\n\t"                               /* ==> Exit */
                         "isync"                                /* instruction sync */
                         : [res]"=&r"(result)                   /* value retrieved from address + offset */
                         , [tmp]"=&r"(temporary)                /* temporary data holder */
                         : [ptr]"r"((void *)((long int)ptr & 0xFFFFFFFFFFFFFFFC))   /* aligned access address */
                         , [off]"r"(offset)                     /* aligned data offset */
                         , [val]"r"(value)                      /* value to replace */
                         , [cmp]"r"(comparand)                  /* value to compare against */
                         , [msk]"r"(maskoff)                    /* mask to use for isolation */
                         : "memory"                             /* compiler full fence */
                         , "cr0"                                /* clobbered by cmpw and/or stwcx. */
                         );

    #elif __TBB_ENDIANNESS==__TBB_ENDIAN_LITTLE
    // [FIXME] Replace this GNU CC built-in with a more efficient ASM implementation for little-endian
    result = __sync_val_compare_and_swap((volatile uint8_t *)ptr, comparand, value);
    #else
        #error "Unsupported endianess found... Aborting!"
    #endif

    return result;
}

inline int16_t __TBB_machine_cmpswp2(volatile void *ptr, int16_t value, int16_t comparand) {
    int16_t result;
    #if __TBB_ENDIANNESS==__TBB_ENDIAN_BIG
    int16_t  temporary;
    int offset = (long int)ptr & 0x3;
    __asm__ __volatile__("0:\n\t"                               /* ==> retry loop */
                         "sync\n\t"                             /* sync every memory access */
                         "lwarx %[res], 0, %[ptr]\n\t"          /* load w/ reservation */
                         "mr %[tmp], %[res]\n\t"                /* keep a copy for later use */
                         "cmpwi %[off], 0\n\t"                  /* if offset is 0 (on BE), shift 16 */
                         "beq 2f\n"                             /* branch to shift code, otherwise no shift */

                         "1:\n\t"                               /* ==> Work on data directly (noshift) */
                         "andi. %[res], %[res], 0xffff\n\t"     /* clean up retrieved value */
                         "cmpw %[cmp], %[res]\n\t"              /* compare against comparand (** dup **) */
                         "bne- 4f\n\t"                          /* exit if not same (** dup **) */
                         "andis. %[tmp], %[tmp], 0xffff\n\t"    /* set the mask on saved value */
                         "or %[val], %[val], %[tmp]\n"          /* combine original value with shifted one for storage */
                         "b 3f\n"                               /* step right into storage */

                         "2:\n\t"                               /* ==> Perform right shift 16 */
                         "srwi %[res], %[res], 16\n\t"          /* perform required shift */
                         "cmpw %[cmp], %[res]\n\t"              /* compare against comparand (** dup **) */
                         "bne- 4f\n\t"                          /* exit if not same (** dup **) */
                         "slwi %[val], %[val], 16\n\t"          /* shift new value to proper position in word */
                         "andi. %[tmp], %[tmp], 0xffff\n\t"     /* set the mask on saved value */
                         "or %[val], %[val], %[tmp]\n\t"        /* combine original value with new one for storage */

                         "3:\n\t"                               /* ==> Storage steps (storage) */
                         "stwcx. %[val], 0, %[ptr]\n\t"         /* reserved store new value */
                         "bne- 0b\n"                            /* retry if reservation lost */

                         "4:\n\t"                               /* ==> Exit */
                         "isync"                                /* instruction sync */
                         : [res]"=&r"(result)                   /* value retrieved from address + offset */
                         , [tmp]"=&r"(temporary)                /* temporary data holder */
                         : [ptr]"r"((void *)((long int)ptr & 0xFFFFFFFFFFFFFFFC))   /* aligned access address */
                         , [off]"r"(offset)                     /* aligned data offset */
                         , [val]"r"(value)                      /* value to replace */
                         , [cmp]"r"(comparand)                  /* value to compare against */
                         : "memory"                             /* compiler full fence */
                         , "cr0"                                /* clobbered by cmpw and/or stwcx. */
                         );
    #elif __TBB_ENDIANNESS==__TBB_ENDIAN_LITTLE
    // [FIXME] Replace this GNU CC built-in with a more efficient ASM implementation for little-endian
    result = __sync_val_compare_and_swap((volatile int16_t *)ptr, comparand, value);
    #else
        #error "Unsupported endianess found... Aborting!"
    #endif
    return result;
}

inline int32_t __TBB_machine_cmpswp4(volatile void *ptr, int32_t value, int32_t comparand) {
    int32_t result;

    __asm__ __volatile__("sync\n"
                         "0:\n\t"
                         "lwarx %[res], 0, %[ptr]\n\t"          /* load w/ reservation */
                         "cmpw %[cmp], %[res]\n\t"              /* compare against comparand */
                         "bne- 1f\n\t"                          /* exit if not same */
                         "stwcx. %[val], 0, %[ptr]\n\t"         /* store new value */
                         "bne- 0b\n"                            /* retry if reservation lost */
                         "1:\n\t"                               /* the exit */
                         "isync"
                         : [res]"=&r"(result)
                         , "+m"(* (int32_t*) ptr)               /* redundant with "memory" */
                         : [ptr]"r"(ptr)
                         , [val]"r"(value)
                         , [cmp]"r"(comparand)
                         : "memory"                             /* compiler full fence */
                         , "cr0"                                /* clobbered by cmpx and/or strx. */
                         );
    return result;
};

#if __TBB_WORDSIZE==8

inline int64_t __TBB_machine_cmpswp8(volatile void *ptr, int64_t value, int64_t comparand) {
    int64_t result;

    __asm__ __volatile__("sync\n"
                         "0:\n\t"
                         "ldarx %[res], 0, %[ptr]\n\t"          /* load w/ reservation */
                         "cmpd %[cmp], %[res]\n\t"              /* compare against comparand */
                         "bne- 1f\n\t"                          /* exit if not same */
                         "stdcx. %[val], 0, %[ptr]\n\t"         /* store new value */
                         "bne- 0b\n"                            /* retry if reservation lost */
                         "1:\n\t"                               /* the exit */
                         "isync"
                         : [res]"=&r"(result)
                         , "+m"(* (int64_t*) ptr)               /* redundant with "memory" */
                         : [ptr]"r"(ptr)
                         , [val]"r"(value)
                         , [cmp]"r"(comparand)
                         : "memory"                             /* compiler full fence */
                         , "cr0"                                /* clobbered by cmpx and/or strx. */
                         );
    return result;
};

#elif __TBB_64BIT_ATOMICS /* && __TBB_WORDSIZE==4 */

inline int64_t __TBB_machine_cmpswp8(volatile void *ptr, int64_t value, int64_t comparand) {
    int64_t result;
    int64_t value_register, comparand_register, result_register; // dummy variables to allocate registers
    __asm__ __volatile__("sync\n\t"
                         "ld %[val], %[valm]\n\t"
                         "ld %[cmp], %[cmpm]\n"
                         "0:\n\t"
                         "ldarx %[res], 0, %[ptr]\n\t"   /* load w/ reservation */
                         "cmpd %[cmp], %[res]\n\t"       /* compare against comparand */
                         "bne- 1f\n\t"                   /* exit if not same */
                         "stdcx. %[val], 0, %[ptr]\n\t"  /* store new value */
                         "bne- 0b\n"                     /* retry if reservation lost */
                         "1:\n\t"                        /* the exit */
                         "std %[res], %[resm]\n\t"
                         "isync"
                         : [resm]"=m"(result)
                         , [res] "=&r"(result_register)
                         , [val] "=&r"(value_register)
                         , [cmp] "=&r"(comparand_register)
                         , "+m"(* (int64_t*) ptr)        /* redundant with "memory" */
                         : [ptr] "r"(ptr)
                         , [valm]"m"(value)
                         , [cmpm]"m"(comparand)
                         : "memory"                      /* compiler full fence */
                         , "cr0"                         /* clobbered by cmpd and/or stdcx. */
                         );
    return result;
}

#endif /* __TBB_WORDSIZE==4 && __TBB_64BIT_ATOMICS */

#define __TBB_MACHINE_DEFINE_LOAD_STORE(S, ldx, stx, cmpx)                                                    \
    template <typename T>                                                                                     \
    struct machine_load_store<T,S> {                                                                          \
        static inline T load_with_acquire(const volatile T& location) {                                       \
            T result;                                                                                         \
            __asm__ __volatile__(ldx " %[res], 0(%[ptr])\n"                                                   \
                                 "0:\n\t"                                                                     \
                                 cmpx " %[res], %[res]\n\t"                                                   \
                                 "bne- 0b\n\t"                                                                \
                                 "isync"                                                                      \
                                 : [res]"=r"(result)                                                          \
                                 : [ptr]"b"(&location) /* cannot use register 0 here */                       \
                                 , "m"(location)       /* redundant with "memory" */                          \
                                 : "memory"            /* compiler acquire fence */                           \
                                 , "cr0"               /* clobbered by cmpw/cmpd */                           \
                                 );                                                                           \
            return result;                                                                                    \
        }                                                                                                     \
        static inline void store_with_release(volatile T &location, T value) {                                \
            __asm__ __volatile__("lwsync\n\t"                                                                 \
                                 stx " %[val], 0(%[ptr])"                                                     \
                                 : "=m"(location)      /* redundant with "memory" */                          \
                                 : [ptr]"b"(&location) /* cannot use register 0 here */                       \
                                 , [val]"r"(value)                                                            \
                                 : "memory"/*compiler release fence*/ /*(cr0 not affected)*/                  \
                                 );                                                                           \
        }                                                                                                     \
    };                                                                                                        \
                                                                                                              \
    template <typename T>                                                                                     \
    struct machine_load_store_relaxed<T,S> {                                                                  \
        static inline T load (const __TBB_atomic T& location) {                                               \
            T result;                                                                                         \
            __asm__ __volatile__(ldx " %[res], 0(%[ptr])"                                                     \
                                 : [res]"=r"(result)                                                          \
                                 : [ptr]"b"(&location) /* cannot use register 0 here */                       \
                                 , "m"(location)                                                              \
                                 ); /*(no compiler fence)*/ /*(cr0 not affected)*/                            \
            return result;                                                                                    \
        }                                                                                                     \
        static inline void store (__TBB_atomic T &location, T value) {                                        \
            __asm__ __volatile__(stx " %[val], 0(%[ptr])"                                                     \
                                 : "=m"(location)                                                             \
                                 : [ptr]"b"(&location) /* cannot use register 0 here */                       \
                                 , [val]"r"(value)                                                            \
                                 ); /*(no compiler fence)*/ /*(cr0 not affected)*/                            \
        }                                                                                                     \
    };

namespace tbb {
namespace internal {
    __TBB_MACHINE_DEFINE_LOAD_STORE(1,"lbz","stb","cmpw")
    __TBB_MACHINE_DEFINE_LOAD_STORE(2,"lhz","sth","cmpw")
    __TBB_MACHINE_DEFINE_LOAD_STORE(4,"lwz","stw","cmpw")
#if __TBB_WORDSIZE==8
    __TBB_MACHINE_DEFINE_LOAD_STORE(8,"ld" ,"std","cmpd")
#elif __TBB_64BIT_ATOMICS /* && __TBB_WORDSIZE==4 */
    template <typename T>
    struct machine_load_store<T,8> {
        static inline T load_with_acquire(const volatile T& location) {
            T result;
            T result_register; // dummy variable to allocate a register
            __asm__ __volatile__("ld %[res], 0(%[ptr])\n\t"
                                 "std %[res], %[resm]\n"
                                 "0:\n\t"
                                 "cmpd %[res], %[res]\n\t"
                                 "bne- 0b\n\t"
                                 "isync"
                                 : [resm]"=m"(result)
                                 , [res]"=&r"(result_register)
                                 : [ptr]"b"(&location) /* cannot use register 0 here */
                                 , "m"(location)       /* redundant with "memory" */
                                 : "memory"            /* compiler acquire fence */
                                 , "cr0"               /* clobbered by cmpd */
                                );
            return result;
        }
        static inline void store_with_release(volatile T &location, T value) {
            T value_register; // dummy variable to allocate a register
            __asm__ __volatile__("lwsync\n\t"
                                 "ld %[val], %[valm]\n\t"
                                 "std %[val], 0(%[ptr])"
                                 : "=m"(location)      /* redundant with "memory" */
                                 , [val]"=&r"(value_register)
                                 : [ptr]"b"(&location) /* cannot use register 0 here */
                                 , [valm]"m"(value)
                                 : "memory"            /*compiler release fence*/
                                );                     /*(cr0 not affected)*/
        }
    };

    struct machine_load_store_relaxed<T,8> {
        static inline T load (const volatile T& location) {
            T result;
            T result_register; // dummy variable to allocate a register
            __asm__ __volatile__("ld %[res], 0(%[ptr])\n\t"
                                 "std %[res], %[resm]"
                                 : [resm]"=m"(result)
                                 , [res]"=&r"(result_register)
                                 : [ptr]"b"(&location) /* cannot use register 0 here */
                                 , "m"(location)
                                );   /*(no compiler fence)*/ /*(cr0 not affected)*/
            return result;
        }
        static inline void store (volatile T &location, T value) {
            T value_register; // dummy variable to allocate a register
            __asm__ __volatile__("ld %[val], %[valm]\n\t"
                                 "std %[val], 0(%[ptr])"
                                 : "=m"(location)
                                 , [val]"=&r"(value_register)
                                 : [ptr]"b"(&location) /* cannot use register 0 here */
                                 , [valm]"m"(value)
                                 );  /*(no compiler fence)*/ /*(cr0 not affected)*/
        }
    };
    #define __TBB_machine_load_store_relaxed_8
#endif /* __TBB_WORDSIZE==4 && __TBB_64BIT_ATOMICS */

}} // namespaces internal, tbb

#undef __TBB_MACHINE_DEFINE_LOAD_STORE
#undef __TBB_USE_GENERIC_PART_WORD_CAS

#define __TBB_USE_GENERIC_FETCH_ADD                         1
#define __TBB_USE_GENERIC_FETCH_STORE                       1
#define __TBB_USE_GENERIC_SEQUENTIAL_CONSISTENCY_LOAD_STORE 1

#define __TBB_control_consistency_helper() __asm__ __volatile__( "isync": : :"memory")
#define __TBB_acquire_consistency_helper() __asm__ __volatile__("lwsync": : :"memory")
#define __TBB_release_consistency_helper() __asm__ __volatile__("lwsync": : :"memory")
#define __TBB_full_memory_fence()          __asm__ __volatile__(  "sync": : :"memory")

static inline intptr_t __TBB_machine_lg(uintptr_t x) {
    __TBB_ASSERT(x, "__TBB_Log2(0) undefined");
    // cntlzd/cntlzw starts counting at 2^63/2^31 (ignoring any higher-order bits), and does not affect cr0
#if __TBB_WORDSIZE==8
    __asm__ __volatile__ ("cntlzd %0, %0" : "+r"(x));
    return 63-static_cast<intptr_t>(x);
#else
    __asm__ __volatile__ ("cntlzw %0, %0" : "+r"(x));
    return 31-static_cast<intptr_t>(x);
#endif
}
#define __TBB_Log2(V) __TBB_machine_lg(V)

// Assumes implicit alignment for any 32-bit value
typedef uint32_t __TBB_Flag;
#define __TBB_Flag __TBB_Flag

inline bool __TBB_machine_trylockbyte(__TBB_atomic __TBB_Flag &flag) {
    return __TBB_machine_cmpswp4(&flag,1,0)==0;
}
#define __TBB_TryLockByte(P) __TBB_machine_trylockbyte(P)

// Restore previous diagnostic compiler options
#pragma GCC diagnostic pop
