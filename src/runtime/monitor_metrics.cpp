#include "minilisysm/runtime/monitor_metrics.hpp"

#include <algorithm>

namespace lisysm {
namespace {

size_t event_type_index(EventType type) {
    const size_t index = static_cast<size_t>(type);
    return index < 12 ? index : 0;
}

size_t event_level_index(EventLevel level) {
    const size_t index = static_cast<size_t>(level);
    return index < 4 ? index : 0;
}

} // namespace

MonitorMetrics::MonitorMetrics(const MonitorConfig& config) : config_(config) {}

void MonitorMetrics::record_event(const InternalEvent& event) {
    event_type_counts_[event_type_index(event.event_type)].fetch_add(1, std::memory_order_relaxed);
    event_level_counts_[event_level_index(event.level)].fetch_add(1, std::memory_order_relaxed);
    if (config_.metrics_scrape_runtime) {
        registry_.inc_counter("minilisysm_events_published_total");
        registry_.inc_counter("minilisysm_events_by_type_total", 1.0, {{"type", to_string(event.event_type)}});
        registry_.inc_counter("minilisysm_events_by_level_total", 1.0, {{"level", to_string(event.level)}});
    }
}

void MonitorMetrics::record_collector_elapsed(const char* collector, uint64_t elapsed_ms) {
    registry_.set_gauge("minilisysm_collector_elapsed_ms", static_cast<double>(elapsed_ms), {{"collector", collector}});
}

void MonitorMetrics::record_collector_overrun(const char* collector) {
    registry_.inc_counter("minilisysm_collector_overruns_total", 1.0, {{"collector", collector}});
}

void MonitorMetrics::record_meminfo(const MeminfoSample& sample) {
    if (!config_.metrics_scrape_collectors || !sample.valid) {
        return;
    }
    registry_.set_gauge("minilisysm_system_memory_total_bytes", static_cast<double>(sample.mem_total_kb) * 1024.0);
    registry_.set_gauge("minilisysm_system_memory_available_bytes",
                        static_cast<double>(sample.mem_available_kb) * 1024.0);
}

void MonitorMetrics::record_cpu_usage(const CpuUsageSample& sample) {
    if (!config_.metrics_scrape_collectors || !sample.valid) {
        return;
    }
    registry_.set_gauge("minilisysm_cpu_usage_percent", sample.usage_percent, {{"cpu", sample.cpu}});
    registry_.set_gauge("minilisysm_cpu_delta_total_jiffies", static_cast<double>(sample.delta_total_jiffies),
                        {{"cpu", sample.cpu}});
    registry_.set_gauge("minilisysm_cpu_delta_idle_jiffies", static_cast<double>(sample.delta_idle_jiffies),
                        {{"cpu", sample.cpu}});
}

void MonitorMetrics::record_self_status(const SelfStatusSample& sample) {
    if (!config_.metrics_scrape_collectors || !sample.valid) {
        return;
    }
    registry_.set_gauge("minilisysm_monitor_rss_bytes", static_cast<double>(sample.vm_rss_kb) * 1024.0);
}

void MonitorMetrics::record_hardware_health(const HardwareHealthSample& sample) {
    if (!config_.metrics_scrape_collectors) {
        return;
    }
    std::vector<MetricSample> battery_capacity;
    std::vector<MetricSample> battery_health;
    std::vector<MetricSample> battery_cycles;
    std::vector<MetricSample> battery_temperature;
    std::vector<MetricSample> storage_lifetime;
    std::vector<MetricSample> storage_pre_eol;
    battery_capacity.reserve(sample.batteries.size());
    battery_health.reserve(sample.batteries.size());
    battery_cycles.reserve(sample.batteries.size());
    battery_temperature.reserve(sample.batteries.size());
    storage_lifetime.reserve(sample.storage_devices.size());
    storage_pre_eol.reserve(sample.storage_devices.size());

    for (const BatteryHealthSample& battery : sample.batteries) {
        const std::vector<MetricLabel> labels{{"battery", battery.name}};
        if (battery.capacity_percent >= 0.0) {
            battery_capacity.push_back(MetricSample{battery.capacity_percent, labels});
        }
        if (battery.health_percent >= 0.0) {
            battery_health.push_back(MetricSample{battery.health_percent, labels});
        }
        if (battery.cycle_count >= 0) {
            battery_cycles.push_back(MetricSample{static_cast<double>(battery.cycle_count), labels});
        }
        if (battery.temperature_celsius >= 0.0) {
            battery_temperature.push_back(MetricSample{battery.temperature_celsius, labels});
        }
    }
    for (const StorageHealthSample& storage : sample.storage_devices) {
        const std::vector<MetricLabel> labels{{"device", storage.device}};
        if (storage.lifetime_used_percent >= 0.0) {
            storage_lifetime.push_back(MetricSample{storage.lifetime_used_percent, labels});
        }
        if (storage.pre_eol_info >= 0) {
            storage_pre_eol.push_back(MetricSample{static_cast<double>(storage.pre_eol_info), labels});
        }
    }

    registry_.set_gauge_family("minilisysm_battery_capacity_percent", std::move(battery_capacity));
    registry_.set_gauge_family("minilisysm_battery_health_percent", std::move(battery_health));
    registry_.set_gauge_family("minilisysm_battery_cycle_count", std::move(battery_cycles));
    registry_.set_gauge_family("minilisysm_battery_temperature_celsius", std::move(battery_temperature));
    registry_.set_gauge_family("minilisysm_storage_lifetime_used_percent", std::move(storage_lifetime));
    registry_.set_gauge_family("minilisysm_storage_pre_eol_info", std::move(storage_pre_eol));
    if (sample.memory.valid) {
        registry_.set_gauge("minilisysm_memory_ecc_corrected_total",
                            static_cast<double>(sample.memory.ecc_corrected_errors));
        registry_.set_gauge("minilisysm_memory_ecc_uncorrected_total",
                            static_cast<double>(sample.memory.ecc_uncorrected_errors));
    }
}

void MonitorMetrics::record_whitelisted_process_status(const std::string& process, bool up) {
    if (!config_.metrics_scrape_collectors) {
        return;
    }
    registry_.set_gauge("minilisysm_whitelisted_process_up", up ? 1.0 : 0.0, {{"process", process}});
}

void MonitorMetrics::record_whitelisted_process_sample(const std::string& process, int pid, uint64_t rss_bytes,
                                                       uint64_t threads, double cpu_percent) {
    if (!config_.metrics_scrape_collectors) {
        return;
    }
    const std::vector<MetricLabel> labels{{"process", process}, {"pid", std::to_string(pid)}};
    registry_.set_gauge("minilisysm_whitelisted_process_rss_bytes", static_cast<double>(rss_bytes), labels);
    registry_.set_gauge("minilisysm_whitelisted_process_threads", static_cast<double>(threads), labels);
    registry_.set_gauge("minilisysm_whitelisted_process_cpu_usage_percent", cpu_percent, labels);
}

void MonitorMetrics::record_sched_delay(const std::vector<SchedDelaySample>& samples) {
    if (!config_.metrics_scrape_collectors) {
        return;
    }
    std::vector<MetricSample> wait_samples;
    std::vector<MetricSample> switch_samples;
    std::vector<MetricSample> max_wait_samples;
    std::vector<MetricSample> avg_wait_samples;
    std::vector<MetricSample> aggregate_samples;
    wait_samples.reserve(samples.size());
    switch_samples.reserve(samples.size());
    max_wait_samples.reserve(samples.size());
    avg_wait_samples.reserve(samples.size());
    aggregate_samples.reserve(samples.size());
    for (const SchedDelaySample& sample : samples) {
        if (!sample.valid) {
            continue;
        }
        const std::string pid = std::to_string(sample.pid);
        const std::string tid = std::to_string(sample.tid);
        std::vector<MetricLabel> labels{{"pid", pid}, {"tid", tid}};
        wait_samples.push_back(MetricSample{static_cast<double>(sample.delta_wait_sum_us), labels});
        switch_samples.push_back(MetricSample{static_cast<double>(sample.delta_involuntary_switches), labels});
        max_wait_samples.push_back(MetricSample{static_cast<double>(sample.max_wait_us), labels});
        avg_wait_samples.push_back(MetricSample{static_cast<double>(sample.avg_wait_us), labels});
        aggregate_samples.push_back(MetricSample{static_cast<double>(sample.aggregate_count), std::move(labels)});
    }
    registry_.set_gauge_family("minilisysm_sched_wait_us", std::move(wait_samples));
    registry_.set_gauge_family("minilisysm_sched_involuntary_switches", std::move(switch_samples));
    registry_.set_gauge_family("minilisysm_sched_max_wait_us", std::move(max_wait_samples));
    registry_.set_gauge_family("minilisysm_sched_avg_wait_us", std::move(avg_wait_samples));
    registry_.set_gauge_family("minilisysm_sched_aggregate_count", std::move(aggregate_samples));
}

void MonitorMetrics::record_io_delay(const IoDelaySample& sample) {
    if (!config_.metrics_scrape_collectors || !sample.valid) {
        return;
    }
    registry_.set_gauge("minilisysm_io_await_ms", sample.avg_await_ms, {{"device", sample.device}});
    registry_.set_gauge("minilisysm_io_util_percent", sample.util_percent, {{"device", sample.device}});
    registry_.set_gauge("minilisysm_io_delta_count", static_cast<double>(sample.delta_io_count),
                        {{"device", sample.device}});
}

std::string MonitorMetrics::render(bool running, uint64_t next_sequence, const QueueSnapshot& queues,
                                   const std::vector<std::pair<std::string, SinkStats>>& sink_stats,
                                   uint64_t meminfo_failures, uint64_t cpu_usage_failures,
                                   uint64_t self_status_failures, uint64_t sched_delay_failures,
                                   uint64_t io_delay_failures,
                                   const SchedDelayCollectorRuntimeStats& sched_stats) const {
    if (config_.metrics_scrape_runtime) {
        registry_.set_gauge("minilisysm_up", running ? 1.0 : 0.0);
        registry_.set_counter("minilisysm_events_published_total", static_cast<double>(next_sequence - 1));
        registry_.set_gauge("minilisysm_queue_depth", static_cast<double>(queues.depth), {{"queue", "source"}});
        registry_.set_gauge("minilisysm_queue_depth", static_cast<double>(queues.sink_depth), {{"queue", "sink"}});
        registry_.set_gauge("minilisysm_queue_capacity", static_cast<double>(queues.capacity), {{"queue", "source"}});
        registry_.set_gauge("minilisysm_queue_capacity", static_cast<double>(queues.sink_capacity),
                            {{"queue", "sink"}});
        registry_.set_counter("minilisysm_queue_dropped_total", static_cast<double>(queues.dropped_count),
                              {{"queue", "source"}});
        registry_.set_counter("minilisysm_queue_dropped_total", static_cast<double>(queues.sink_dropped_count),
                              {{"queue", "sink"}});
        registry_.set_gauge("minilisysm_queue_high_watermark", static_cast<double>(queues.high_watermark),
                            {{"queue", "source"}});
        registry_.set_gauge("minilisysm_queue_high_watermark", static_cast<double>(queues.sink_high_watermark),
                            {{"queue", "sink"}});
        registry_.set_counter("minilisysm_dispatcher_sink_push_failures_total",
                              static_cast<double>(queues.dispatcher_sink_push_failures));
        for (const auto& item : sink_stats) {
            const std::vector<MetricLabel> label{{"sink", item.first}};
            const SinkStats& stats = item.second;
            registry_.set_gauge("minilisysm_sink_queue_depth", static_cast<double>(stats.queue_depth), label);
            registry_.set_gauge("minilisysm_sink_queue_capacity", static_cast<double>(stats.queue_capacity), label);
            registry_.set_counter("minilisysm_sink_dropped_total", static_cast<double>(stats.dropped_events), label);
            registry_.set_counter("minilisysm_network_sent_total", static_cast<double>(stats.sent_events), label);
            registry_.set_counter("minilisysm_network_send_errors_total", static_cast<double>(stats.send_errors),
                                  label);
            registry_.set_counter("minilisysm_network_retries_total", static_cast<double>(stats.retry_count), label);
            registry_.set_gauge("minilisysm_network_wal_pending_events", static_cast<double>(stats.wal_pending_events),
                                label);
            registry_.set_gauge("minilisysm_network_wal_bytes", static_cast<double>(stats.wal_bytes), label);
            registry_.set_counter("minilisysm_network_wal_overflow_dropped_total",
                                  static_cast<double>(stats.wal_overflow_dropped_events), label);
        }
    }
    if (config_.metrics_scrape_collectors) {
        registry_.set_counter("minilisysm_collector_failures_total", static_cast<double>(meminfo_failures),
                              {{"collector", "meminfo"}});
        registry_.set_counter("minilisysm_collector_failures_total", static_cast<double>(cpu_usage_failures),
                              {{"collector", "cpu_usage"}});
        registry_.set_counter("minilisysm_collector_failures_total", static_cast<double>(self_status_failures),
                              {{"collector", "self_status"}});
        registry_.set_counter("minilisysm_collector_failures_total", static_cast<double>(sched_delay_failures),
                              {{"collector", "sched_delay"}});
        registry_.set_counter("minilisysm_collector_failures_total", static_cast<double>(io_delay_failures),
                              {{"collector", "io_delay"}});
        registry_.set_counter("minilisysm_ebpf_ringbuf_drops_total",
                              static_cast<double>(sched_stats.ebpf_ringbuf_drops));
        registry_.set_counter("minilisysm_ebpf_allowlist_exec_seen_total",
                              static_cast<double>(sched_stats.ebpf_allowlist_exec_seen));
        registry_.set_counter("minilisysm_ebpf_allowlist_exit_cleaned_total",
                              static_cast<double>(sched_stats.ebpf_allowlist_exit_cleaned));
        registry_.set_counter("minilisysm_ebpf_allowlist_stale_hits_total",
                              static_cast<double>(sched_stats.ebpf_allowlist_stale_hits));
        registry_.set_counter("minilisysm_ebpf_aggregate_drops_total",
                              static_cast<double>(sched_stats.ebpf_aggregate_drops));
        registry_.set_gauge("minilisysm_ebpf_allowlist_scanned_processes",
                            static_cast<double>(sched_stats.allowlist_scanned_processes));
        registry_.set_gauge("minilisysm_ebpf_allowlist_matched_pids",
                            static_cast<double>(sched_stats.allowlist_matched_pids));
        registry_.set_gauge("minilisysm_ebpf_allowlist_matched_tids",
                            static_cast<double>(sched_stats.allowlist_matched_tids));
        registry_.set_gauge("minilisysm_ebpf_allowlist_refresh_elapsed_ms",
                            static_cast<double>(sched_stats.allowlist_refresh_elapsed_ms));
    }
    return registry_.render_prometheus();
}

} // namespace lisysm
