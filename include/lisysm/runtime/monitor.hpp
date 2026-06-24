#pragma once

#include "lisysm/core/config.hpp"
#include "lisysm/rules/rule_engine.hpp"
#include "lisysm/queue/spsc_ring_buffer.hpp"

#include <atomic>
#include <memory>
#include <thread>
#include <vector>

namespace lisysm {

class EventStore;
class MeminfoCollector;
class SchedDelayCollector;
class SelfStatusCollector;

class Monitor {
public:
    explicit Monitor(MonitorConfig config);
    ~Monitor();

    Monitor(const Monitor&) = delete;
    Monitor& operator=(const Monitor&) = delete;

    bool start();
    void stop();
    bool running() const { return running_.load(); }

private:
    void fast_collect_loop();
    void sched_collect_loop();
    bool publish_event(SpscRingBuffer<InternalEvent>& queue, const InternalEvent& event);
    void publish_collector_failure(
        SpscRingBuffer<InternalEvent>& queue,
        uint32_t collector_id,
        uint64_t& total_failures,
        uint64_t& last_event_ms);
    void publish_started_event();
    QueueSnapshot queue_snapshot() const;

    MonitorConfig config_;
    SpscRingBuffer<InternalEvent> fast_queue_;
    SpscRingBuffer<InternalEvent> sched_queue_;
    std::vector<SpscRingBuffer<InternalEvent>*> event_queues_;
    std::unique_ptr<EventStore> store_;
    std::unique_ptr<MeminfoCollector> meminfo_;
    std::unique_ptr<SelfStatusCollector> self_status_;
    std::unique_ptr<SchedDelayCollector> sched_delay_;
    std::unique_ptr<RuleEngine> fast_rules_;
    std::unique_ptr<RuleEngine> sched_rules_;
    std::atomic<bool> running_{false};
    std::thread fast_collector_;
    std::thread sched_collector_;
    std::atomic<uint64_t> next_sequence_{1};
    uint64_t meminfo_failures_{0};
    uint64_t self_status_failures_{0};
    uint64_t sched_delay_failures_{0};
    uint64_t last_meminfo_failure_event_ms_{0};
    uint64_t last_self_status_failure_event_ms_{0};
    uint64_t last_sched_delay_failure_event_ms_{0};
};

} // namespace lisysm
