/* SPDX-License-Identifier: GPL-2.0-or-later OR MIT */
/* Force GCC to use LEA (RIP-relative) instead of MOV imm32 for function addresses.
 * Without this, -Os generates 32-bit immediates that break at high load addresses. */
#ifndef FUNCPTR_FIX_H
#define FUNCPTR_FIX_H
#define FORCE_FUNCPTR(func)                                          \
    ({                                                               \
        void *_addr;                                                 \
        __asm__("leaq " #func "(%%rip), %0" : "=r"(_addr));          \
        _addr;                                                       \
    })
#endif
