// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#pragma once

#include <cstdint>

#if defined(__x86_64__) || defined(_M_X64)
#include <immintrin.h>
#elif !defined(__aarch64__)
#error "NAMsayin builds for x86-64 and arm64 (macOS) only"
#endif

namespace namsayin {

// The CPU's floating-point control state (x86: MXCSR; arm64: FPCR)
using FloatState = uint64_t;

inline FloatState floatState()
{
#if defined(__aarch64__)
    uint64_t fpcr = 0;
    __asm__ __volatile__("mrs %0, fpcr" : "=r"(fpcr));
    return fpcr;
#else
    return _mm_getcsr();
#endif
}

inline void setFloatState(FloatState state)
{
#if defined(__aarch64__)
    __asm__ __volatile__("msr fpcr, %0" : : "r"(state));
#else
    _mm_setcsr(static_cast<unsigned>(state));
#endif
}

// Flushes denormals to zero on this thread, as hosts do on their audio threads (x86: FTZ and DAZ; arm64: FZ).
// Returns the state before, for setFloatState().
inline FloatState flushDenormals()
{
    const FloatState before = floatState();
#if defined(__aarch64__)
    setFloatState(before | (uint64_t { 1 } << 24));
#else
    setFloatState(before | 0x8040);
#endif
    return before;
}

// A hint inside a spin-wait loop
inline void cpuPause()
{
#if defined(__aarch64__)
    __asm__ __volatile__("yield");
#else
    _mm_pause();
#endif
}

} // namespace namsayin
