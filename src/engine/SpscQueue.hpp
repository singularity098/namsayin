// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#pragma once

#include <array>
#include <atomic>
#include <cstddef>

namespace namsayin {

// Lock-free single-producer single-consumer queue with fixed capacity
template <typename T, size_t Capacity>
class SpscQueue {
public:
    bool push(const T& value)
    {
        const size_t head = fHead.load(std::memory_order_relaxed);
        const size_t next = (head + 1) % (Capacity + 1);
        if (next == fTail.load(std::memory_order_acquire))
            return false;
        fItems[head] = value;
        fHead.store(next, std::memory_order_release);
        return true;
    }

    bool pop(T& value)
    {
        const size_t tail = fTail.load(std::memory_order_relaxed);
        if (tail == fHead.load(std::memory_order_acquire))
            return false;
        value = fItems[tail];
        fTail.store((tail + 1) % (Capacity + 1), std::memory_order_release);
        return true;
    }

private:
    std::array<T, Capacity + 1> fItems {};
    std::atomic<size_t> fHead { 0 };
    std::atomic<size_t> fTail { 0 };
};

} // namespace namsayin
