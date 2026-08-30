#pragma once

#include "minilisysm/core/config.hpp"
#include "minilisysm/interfaces/sched_delay_collector.hpp"
#include "minilisysm/collectors/self_status_collector.hpp"
#include "minilisysm/rules/rule_engine.hpp"
#include "minilisysm/queue/spsc_ring_buffer.hpp"
#include "minilisysm/runtime/monitor_metrics.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace lisysm {

class EventDispatcherGroup;
class CpuUsageCollector;
class HardwareHealthCollector;
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
    void record_whitelisted_process_metrics();

    MonitorConfig config_;
    mutable MonitorMetrics metrics_;
    SpscRingBuffer<InternalEvent> fast_queue_;
    SpscRingBuffer<InternalEvent> sched_queue_;
    std::vector<SpscRingBuffer<InternalEvent>*> event_queues_;
    std::unique_ptr<EventDispatcherGroup> dispatcher_;
    std::unique_ptr<MeminfoCollector> meminfo_;
    std::unique_ptr<CpuUsageCollector> cpu_usage_;
    std::unique_ptr<SelfStatusCollector> self_status_;
    std::unique_ptr<HardwareHealthCollector> hardware_health_;
    std::unique_ptr<SchedDelayCollectorInterface> sched_delay_;
    std::unique_ptr<IoDelayCollector> io_delay_;
    std::unique_ptr<MetricsServer> metrics_server_;
    std::unique_ptr<RuleEngine> fast_rules_;
    std::unique_ptr<RuleEngine> sched_rules_;
    std::atomic<bool> running_{false};
    std::mutex lifecycle_mutex_;
    std::mutex stop_mutex_;
    std::condition_variable stop_condition_;
    std::thread fast_collector_;
    std::thread sched_collector_;
    std::atomic<uint64_t> next_sequence_{1};
    std::atomic<uint64_t> meminfo_failures_{0};
    std::atomic<uint64_t> cpu_usage_failures_{0};
    std::atomic<uint64_t> self_status_failures_{0};
    std::atomic<uint64_t> sched_delay_failures_{0};
    std::atomic<uint64_t> io_delay_failures_{0};
    struct ProcessCpuBaseline {
        uint64_t ticks{0};
        std::chrono::steady_clock::time_point sampled_at{};
    };
    std::unordered_map<int, ProcessCpuBaseline> process_cpu_baselines_;
    struct ProcessInstance {
        std::string name;
        uint64_t starttime_ticks{0};
    };
    std::unordered_map<int, ProcessInstance> process_instances_;
    struct ProcessMemorySample {
        std::chrono::steady_clock::time_point sampled_at{};
        uint64_t rss_bytes{0};
    };
    std::unordered_map<int, std::deque<ProcessMemorySample>> process_memory_history_;
    uint64_t last_meminfo_failure_event_ms_{0};
    uint64_t last_cpu_usage_failure_event_ms_{0};
    uint64_t last_self_status_failure_event_ms_{0};
    uint64_t last_sched_delay_failure_event_ms_{0};
    uint64_t last_io_delay_failure_event_ms_{0};
};

} // namespace lisysm
