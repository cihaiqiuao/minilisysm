#pragma once

#include "minilisysm/collectors/cpu_usage_collector.hpp"
#include "minilisysm/collectors/hardware_health_collector.hpp"
#include "minilisysm/collectors/io_delay_collector.hpp"
#include "minilisysm/collectors/meminfo_collector.hpp"
#include "minilisysm/collectors/self_status_collector.hpp"
#include "minilisysm/core/config.hpp"
#include "minilisysm/interfaces/event_sink.hpp"
#include "minilisysm/interfaces/sched_delay_collector.hpp"
#include "minilisysm/rules/rule_engine.hpp"
#include "minilisysm/runtime/metric_registry.hpp"
#include "minilisysm/storage/event_serializer.hpp"

#include <array>
#include <atomic>
#include <string>
#include <utility>
#include <vector>

namespace lisysm {

class MonitorMetrics {
  public:
    explicit MonitorMetrics(const MonitorConfig& config);

    void record_event(const InternalEvent& event);
    void record_collector_elapsed(const char* collector, uint64_t elapsed_ms);
    void record_collector_overrun(const char* collector);
    void record_meminfo(const MeminfoSample& sample);
    void record_cpu_usage(const CpuUsageSample& sample);
    void record_self_status(const SelfStatusSample& sample);
    void record_hardware_health(const HardwareHealthSample& sample);
    void record_whitelisted_process_status(const std::string& process, bool up);
    void record_whitelisted_process_sample(const std::string& process, int pid, uint64_t rss_bytes,
                                           uint64_t threads, double cpu_percent);
    void record_sched_delay(const std::vector<SchedDelaySample>& samples);
    void record_io_delay(const IoDelaySample& sample);

    std::string render(bool running, uint64_t next_sequence, const QueueSnapshot& queues,
                       const std::vector<std::pair<std::string, SinkStats>>& sink_stats, uint64_t meminfo_failures,
                       uint64_t cpu_usage_failures, uint64_t self_status_failures, uint64_t sched_delay_failures,
                       uint64_t io_delay_failures, const SchedDelayCollectorRuntimeStats& sched_stats) const;

  private:
    const MonitorConfig& config_;
    mutable MetricRegistry registry_;
    std::array<std::atomic<uint64_t>, 12> event_type_counts_{};
    std::array<std::atomic<uint64_t>, 4> event_level_counts_{};
};

} // namespace lisysm
