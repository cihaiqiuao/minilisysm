#pragma once

#include "minilisysm/core/event.hpp"
#include "minilisysm/queue/spsc_ring_buffer.hpp"

#include <cstddef>
#include <cstdint>

namespace lisysm {

struct SinkStats {
    uint64_t accepted_events{0};
    uint64_t dropped_events{0};
    uint64_t dropped_critical_events{0};
    uint64_t reserve_reject_events{0};
    uint64_t written_events{0};
    uint64_t write_errors{0};
    uint64_t rotated_files{0};
    uint64_t fsync_count{0}; // Successful durable syncs.
    uint64_t fsync_failures{0};
    uint64_t fsync_rate_limited{0};
    uint64_t sent_events{0};
    uint64_t send_errors{0};
    uint64_t retry_count{0};
    uint64_t wal_pending_events{0};
    uint64_t wal_bytes{0};
    uint64_t wal_overflow_dropped_events{0};
    size_t queue_depth{0};
    size_t queue_capacity{0};
    size_t queue_high_watermark{0};
};

class EventSink {
  public:
    virtual ~EventSink() = default;
    virtual const char* name() const = 0;
    virtual SpscRingBuffer<InternalEvent>* add_input_queue(size_t capacity) = 0;
    virtual bool start() = 0;
    virtual void stop() = 0;
    virtual SinkStats stats() const = 0;
};

} // namespace lisysm
