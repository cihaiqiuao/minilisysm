#pragma once

#include "minilisysm/core/config.hpp"
#include "minilisysm/core/event.hpp"
#include "minilisysm/interfaces/event_sink.hpp"
#include "minilisysm/queue/spsc_ring_buffer.hpp"

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace lisysm {

struct DispatcherStats {
    uint64_t consumed_events{0};
    uint64_t sink_queue_push_failures{0};
    uint64_t sink_queue_dropped_events{0};
    uint64_t sink_queue_dropped_critical_events{0};
    uint64_t sink_queue_reserve_reject_events{0};
    size_t sink_queue_depth{0};
    size_t sink_queue_capacity{0};
    size_t sink_queue_high_watermark{0};
};

class EventDispatcher {
  public:
    EventDispatcher(const MonitorConfig& config, SpscRingBuffer<InternalEvent>& source_queue,
                    std::vector<SpscRingBuffer<InternalEvent>*> sink_queues);
    ~EventDispatcher();

    EventDispatcher(const EventDispatcher&) = delete;
    EventDispatcher& operator=(const EventDispatcher&) = delete;

    bool start();
    void stop();
    DispatcherStats stats() const;

  private:
    void run();
    void drain_source();
    void dispatch(const InternalEvent& event);

    const MonitorConfig& config_;
    SpscRingBuffer<InternalEvent>& source_queue_;
    std::vector<SpscRingBuffer<InternalEvent>*> sink_queues_;
    std::atomic<bool> running_{false};
    std::mutex lifecycle_mutex_;
    std::thread worker_;
    mutable std::atomic<uint64_t> consumed_events_{0};
    mutable std::atomic<uint64_t> sink_queue_push_failures_{0};
};

class EventDispatcherGroup {
  public:
    EventDispatcherGroup(const MonitorConfig& config, std::vector<SpscRingBuffer<InternalEvent>*>& source_queues,
                         std::vector<std::unique_ptr<EventSink>> sinks);
    ~EventDispatcherGroup();

    EventDispatcherGroup(const EventDispatcherGroup&) = delete;
    EventDispatcherGroup& operator=(const EventDispatcherGroup&) = delete;

    bool start();
    void stop();
    DispatcherStats stats() const;
    std::vector<std::pair<std::string, SinkStats>> sink_stats() const;
    size_t sink_count() const {
        return sinks_.size();
    }
    size_t dispatcher_count() const {
        return dispatchers_.size();
    }

  private:
    void stop_components();

    const MonitorConfig& config_;
    std::vector<SpscRingBuffer<InternalEvent>*>& source_queues_;
    std::vector<std::unique_ptr<EventSink>> sinks_;
    std::vector<std::unique_ptr<EventDispatcher>> dispatchers_;
    std::atomic<bool> running_{false};
    std::mutex lifecycle_mutex_;
};

} // namespace lisysm
