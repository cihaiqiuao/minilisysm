#pragma once

#include "lisysm/core/event.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace lisysm {

struct QueueStats {
    uint64_t push_fail_count{0};
    uint64_t dropped_info_count{0};
    uint64_t dropped_warning_count{0};
    uint64_t dropped_critical_count{0};
    size_t high_watermark{0};
};

template <typename T>
class SpscRingBuffer {
public:
    explicit SpscRingBuffer(size_t capacity)
        : capacity_(capacity + 1), buffer_(capacity_) {}

    bool push(const T& item, EventLevel level = EventLevel::Info)
    {
        const size_t head = head_.load(std::memory_order_relaxed);
        const size_t next = increment(head);
        if (next == tail_.load(std::memory_order_acquire)) {
            record_drop(level);
            return false;
        }
        buffer_[head] = item;
        head_.store(next, std::memory_order_release);
        update_high_watermark();
        return true;
    }

    bool pop(T& item)
    {
        const size_t tail = tail_.load(std::memory_order_relaxed);
        if (tail == head_.load(std::memory_order_acquire)) {
            return false;
        }
        item = buffer_[tail];
        tail_.store(increment(tail), std::memory_order_release);
        return true;
    }

    size_t depth() const
    {
        const size_t head = head_.load(std::memory_order_acquire);
        const size_t tail = tail_.load(std::memory_order_acquire);
        if (head >= tail) {
            return head - tail;
        }
        return capacity_ - tail + head;
    }

    size_t usable_capacity() const { return capacity_ - 1; }

    QueueStats stats() const
    {
        return QueueStats{
            push_fail_count_.load(),
            dropped_info_count_.load(),
            dropped_warning_count_.load(),
            dropped_critical_count_.load(),
            high_watermark_.load(),
        };
    }

private:
    size_t increment(size_t value) const { return (value + 1) % capacity_; }

    void update_high_watermark()
    {
        const size_t current = depth();
        size_t observed = high_watermark_.load(std::memory_order_relaxed);
        while (current > observed &&
               !high_watermark_.compare_exchange_weak(observed, current, std::memory_order_relaxed)) {
        }
    }

    void record_drop(EventLevel level)
    {
        push_fail_count_.fetch_add(1, std::memory_order_relaxed);
        if (level == EventLevel::Critical) {
            dropped_critical_count_.fetch_add(1, std::memory_order_relaxed);
        } else if (level == EventLevel::Warning) {
            dropped_warning_count_.fetch_add(1, std::memory_order_relaxed);
        } else {
            dropped_info_count_.fetch_add(1, std::memory_order_relaxed);
        }
    }

    const size_t capacity_;
    std::vector<T> buffer_;
    alignas(64) std::atomic<size_t> head_{0};
    alignas(64) std::atomic<size_t> tail_{0};
    std::atomic<uint64_t> push_fail_count_{0};
    std::atomic<uint64_t> dropped_info_count_{0};
    std::atomic<uint64_t> dropped_warning_count_{0};
    std::atomic<uint64_t> dropped_critical_count_{0};
    std::atomic<size_t> high_watermark_{0};
};

} // namespace lisysm
