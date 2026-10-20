// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "TestSupport.hpp"

#include <cstdlib>
#include <new>

// GCC can't see that these replacements pair malloc with free
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic ignored "-Wmismatched-new-delete"
#endif

// Counts heap allocations made while an AllocationGuard is active on the calling thread, so tests can check
// that the audio path never allocates.

namespace testing {

thread_local bool gCountAllocations = false;
std::atomic<int> gAllocations { 0 };

} // namespace testing

void* operator new(std::size_t size)
{
    if (testing::gCountAllocations)
        testing::gAllocations.fetch_add(1, std::memory_order_relaxed);
    if (void* p = std::malloc(size ? size : 1))
        return p;
    throw std::bad_alloc();
}

void* operator new[](std::size_t size)
{
    return operator new(size);
}

void operator delete(void* p) noexcept
{
    std::free(p);
}

void operator delete[](void* p) noexcept
{
    std::free(p);
}

void operator delete(void* p, std::size_t) noexcept
{
    std::free(p);
}

void operator delete[](void* p, std::size_t) noexcept
{
    std::free(p);
}
