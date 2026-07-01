#pragma once

#include "minilisysm/core/config.hpp"
#include "minilisysm/interfaces/sched_delay_collector.hpp"
#include "minilisysm/collectors/self_status_collector.hpp"
#include "minilisysm/rules/rule_engine.hpp"
#include "minilisysm/queue/spsc_ring_buffer.hpp"
#include "minilisysm/runtime/metric_registry.hpp"

#include <array>
#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace lisysm {

class EventDispatcherGroup;
class CpuUsageCollector;
class IoDelayCollector;
class MetricsServer;
class MeminfoCollector;
class SelfStatusCollector;

class Monitor {
  public:
    explicit Monitor(MonitorConfig config);
    ~Monitor();

    Monitor(const Monitor&) = delete;
    Monitor& operator=(const Monitor&) = delete;

    bool start();
    void stop();
    bool running() const {
        return running_.load();
    }

  private:
    void fast_collect_loop();
    void sched_collect_loop();
    bool publish_event(SpscRingBuffer<InternalEvent>& queue, const InternalEvent& event);
    void publish_collector_failure(SpscRingBuffer<InternalEvent>& queue, uint32_t collector_id,
                                   std::atomic<uint64_t>& total_failures, uint64_t& last_event_ms);
    void publish_started_event();
    QueueSnapshot queue_snapshot() const;
    std::string render_metrics() const;
    void record_event_metrics(const InternalEvent& event);
    void record_meminfo_metrics(const MeminfoSample& sample);
    void record_cpu_usage_metrics(const CpuUsageSample& sample);
    void record_self_status_metrics(const SelfStatusSample& sample);
    void record_sched_delay_metrics(const SchedDelaySample& sample);
    void record_io_delay_metrics(const IoDelaySample& sample);

    MonitorConfig config_;
    SpscRingBuffer<InternalEvent> fast_queue_;
    SpscRingBuffer<InternalEvent> sched_queue_;
    std::vector<SpscRingBuffer<InternalEvent>*> event_queues_;
    std::unique_ptr<EventDispatcherGroup> dispatcher_;
    std::unique_ptr<MeminfoCollector> meminfo_;
    std::unique_ptr<CpuUsageCollector> cpu_usage_;
    std::unique_ptr<SelfStatusCollector> self_status_;
    std::unique_ptr<SchedDelayCollectorInterface> sched_delay_;
    std::unique_ptr<IoDelayCollector> io_delay_;
    std::unique_ptr<MetricsServer> metrics_server_;
    std::unique_ptr<RuleEngine> fast_rules_;
    std::unique_ptr<RuleEngine> sched_rules_;
    std::atomic<bool> running_{false};
    std::thread fast_collector_;
    std::thread sched_collector_;
    std::atomic<uint64_t> next_sequence_{1};
    std::atomic<uint64_t> meminfo_failures_{0};
    std::atomic<uint64_t> cpu_usage_failures_{0};
    std::atomic<uint64_t> self_status_failures_{0};
    std::atomic<uint64_t> sched_delay_failures_{0};
    std::atomic<uint64_t> io_delay_failures_{0};
    std::array<std::atomic<uint64_t>, 11> event_type_counts_{};
    std::array<std::atomic<uint64_t>, 4> event_level_counts_{};
    mutable MetricRegistry metrics_;
    uint64_t last_meminfo_failure_event_ms_{0};
    uint64_t last_cpu_usage_failure_event_ms_{0};
    uint64_t last_self_status_failure_event_ms_{0};
    uint64_t last_sched_delay_failure_event_ms_{0};
    uint64_t last_io_delay_failure_event_ms_{0};
};

} // namespace lisysm
