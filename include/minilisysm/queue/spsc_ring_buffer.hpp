#pragma once

#include "minilisysm/core/event.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace lisysm {

struct QueueStats {
    uint64_t push_fail_count{0};
    uint64_t dropped_info_count{0};
    uint64_t dropped_warning_count{0};
    uint64_t dropped_critical_count{0};
    uint64_t reserve_reject_count{0};
    size_t high_watermark{0};
};

template <typename T>
class SpscRingBuffer {
  public:
    explicit SpscRingBuffer(size_t capacity, size_t critical_reserved_slots = 0, bool drop_info_when_reserved = false,
                            bool drop_warning_when_reserved = false)
        : capacity_(capacity + 1), critical_reserved_slots_(critical_reserved_slots),
          drop_info_when_reserved_(drop_info_when_reserved), drop_warning_when_reserved_(drop_warning_when_reserved),
          buffer_(capacity_) {}

    bool push(const T& item, EventLevel level = EventLevel::Info) {
        return emplace(item, level);
    }

    bool push(T&& item, EventLevel level = EventLevel::Info) {
        return emplace(std::move(item), level);
    }

    bool pop(T& item) {
        const size_t tail = tail_.load(std::memory_order_relaxed);
        if (tail == head_.load(std::memory_order_acquire)) {
            return false;
        }
        item = std::move(buffer_[tail]);
        tail_.store(increment(tail), std::memory_order_release);
        return true;
    }

    size_t depth() const {
        const size_t head = head_.load(std::memory_order_acquire);
        const size_t tail = tail_.load(std::memory_order_acquire);
        if (head >= tail) {
            return head - tail;
        }
        return capacity_ - tail + head;
    }

    size_t usable_capacity() const {
        return capacity_ - 1;
    }

    QueueStats stats() const {
        return QueueStats{
            push_fail_count_.load(),        dropped_info_count_.load(),   dropped_warning_count_.load(),
            dropped_critical_count_.load(), reserve_reject_count_.load(), high_watermark_.load(),
        };
    }

  private:
    template <typename U>
    bool emplace(U&& item, EventLevel level) {
        const size_t head = head_.load(std::memory_order_relaxed);
        const size_t next = increment(head);
        if (next == tail_.load(std::memory_order_acquire)) {
            record_drop(level, false);
            return false;
        }
        if (should_reject_for_reserve(level)) {
            record_drop(level, true);
            return false;
        }
        buffer_[head] = std::forward<U>(item);
        head_.store(next, std::memory_order_release);
        update_high_watermark();
        return true;
    }
    size_t increment(size_t value) const {
        return (value + 1) % capacity_;
    }

    void update_high_watermark() {
        const size_t current = depth();
        size_t observed = high_watermark_.load(std::memory_order_relaxed);
        while (current > observed &&
               !high_watermark_.compare_exchange_weak(observed, current, std::memory_order_relaxed)) {
        }
    }

    bool should_reject_for_reserve(EventLevel level) const {
        if (level == EventLevel::Critical || critical_reserved_slots_ == 0) {
            return false;
        }
        if (level == EventLevel::Warning && !drop_warning_when_reserved_) {
            return false;
        }
        if ((level == EventLevel::Info || level == EventLevel::Recovery) && !drop_info_when_reserved_) {
            return false;
        }
        const size_t usable = usable_capacity();
        if (usable == 0) {
            return true;
        }
        const size_t reserved = critical_reserved_slots_ >= usable ? usable - 1 : critical_reserved_slots_;
        const size_t free_slots = usable - depth();
        return free_slots <= reserved;
    }

    void record_drop(EventLevel level, bool rejected_by_reserve) {
        push_fail_count_.fetch_add(1, std::memory_order_relaxed);
        if (rejected_by_reserve) {
            reserve_reject_count_.fetch_add(1, std::memory_order_relaxed);
        }
        if (level == EventLevel::Critical) {
            dropped_critical_count_.fetch_add(1, std::memory_order_relaxed);
        } else if (level == EventLevel::Warning) {
            dropped_warning_count_.fetch_add(1, std::memory_order_relaxed);
        } else {
            dropped_info_count_.fetch_add(1, std::memory_order_relaxed);
        }
    }

    const size_t capacity_;
    const size_t critical_reserved_slots_;
    const bool drop_info_when_reserved_;
    const bool drop_warning_when_reserved_;
    std::vector<T> buffer_;
    alignas(64) std::atomic<size_t> head_{0};
    alignas(64) std::atomic<size_t> tail_{0};
    std::atomic<uint64_t> push_fail_count_{0};
    std::atomic<uint64_t> dropped_info_count_{0};
    std::atomic<uint64_t> dropped_warning_count_{0};
    std::atomic<uint64_t> dropped_critical_count_{0};
    std::atomic<uint64_t> reserve_reject_count_{0};
    std::atomic<size_t> high_watermark_{0};
};

} // namespace lisysm
