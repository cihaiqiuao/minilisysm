#pragma once

#include "lisysm/core/config.hpp"
#include "lisysm/core/event.hpp"
#include "lisysm/queue/spsc_ring_buffer.hpp"

#include <memory>
#include <vector>

namespace lisysm {

class EventStore;

class StorageFactory {
public:
    static std::unique_ptr<EventStore> create_event_store(
        const MonitorConfig& config,
        std::vector<SpscRingBuffer<InternalEvent>*>& queues);
};

} // namespace lisysm
