#include "minilisysm/runtime/monitor.hpp"
#include "minilisysm/collectors/collector_factory.hpp"
#include "minilisysm/collectors/io_delay_collector.hpp"
#include "minilisysm/collectors/meminfo_collector.hpp"
#include "minilisysm/collectors/self_status_collector.hpp"
#include "minilisysm/core/time.hpp"
#include "minilisysm/rules/rule_factory.hpp"
#include "minilisysm/runtime/event_dispatcher.hpp"
#include "minilisysm/runtime/metrics_server.hpp"
#include "minilisysm/runtime/thread_policy.hpp"
#include "minilisysm/storage/storage_factory.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <sstream>
#include <utility>

namespace lisysm {
namespace {

constexpr uint32_t kMeminfoCollectorId = 1;
constexpr uint32_t kSelfStatusCollectorId = 2;
constexpr uint32_t kSchedDelayCollectorId = 3;
constexpr uint32_t kIoDelayCollectorId = 4;
constexpr uint64_t kCollectorFailureEventIntervalMs = 60000;

size_t event_type_index(EventType type) {
    const size_t index = static_cast<size_t>(type);
    return index < 10 ? index : 0;
}

size_t event_level_index(EventLevel level) {
    const size_t index = static_cast<size_t>(level);
    return index < 4 ? index : 0;
}

void set_evidence_key(EvidenceItem& item, const char* key) {
    std::strncpy(item.key.data(), key, kEvidenceKeySize - 1);
    item.key[kEvidenceKeySize - 1] = '\0';
}

std::chrono::steady_clock::time_point next_deadline(std::chrono::steady_clock::time_point current_deadline,
                                                    std::chrono::milliseconds interval) {
    const auto now = std::chrono::steady_clock::now();
    current_deadline += interval;
    if (current_deadline <= now) {
        return now + interval;
    }
    return current_deadline;
}

} // namespace

Monitor::Monitor(MonitorConfig config)
    : config_(std::move(config)), fast_queue_(config_.event_queue_capacity, config_.critical_reserved_slots,
                                              config_.drop_info_when_full, config_.drop_warning_when_full),
      sched_queue_(config_.event_queue_capacity, config_.critical_reserved_slots, config_.drop_info_when_full,
                   config_.drop_warning_when_full),
      event_queues_{&fast_queue_, &sched_queue_},
      dispatcher_(
          std::make_unique<EventDispatcherGroup>(config_, event_queues_, StorageFactory::create_event_sinks(config_))),
      meminfo_(CollectorFactory::create_meminfo_collector()),
      self_status_(CollectorFactory::create_self_status_collector()),
      sched_delay_(CollectorFactory::create_sched_delay_collector(config_)),
      io_delay_(CollectorFactory::create_io_delay_collector(config_)),
      metrics_server_(std::make_unique<MetricsServer>(config_, [this]() { return render_metrics(); })),
      fast_rules_(RuleFactory::create_fast_rule_engine(config_)),
      sched_rules_(RuleFactory::create_sched_rule_engine(config_)) {}

Monitor::~Monitor() {
    stop();
}

bool Monitor::start() {
    if (!config_.enable) {
        spdlog::warn("monitor start skipped: linux_stability_monitor.enable=false");
        return false;
    }
    spdlog::info("monitor starting: fast_interval_ms={} low_freq_interval_ms={} queue_capacity={} sink_count={}",
                 config_.fast_collect_interval_ms, config_.low_freq_collect_interval_ms, config_.event_queue_capacity,
                 dispatcher_->sink_count());
    if (!dispatcher_->start()) {
        spdlog::error("monitor start failed: event dispatcher group failed to start");
        return false;
    }
    if (!metrics_server_->start()) {
        spdlog::error("monitor start failed: metrics server failed to start");
        dispatcher_->stop();
        return false;
    }
    running_.store(true);
    publish_started_event();
    fast_collector_ = std::thread(&Monitor::fast_collect_loop, this);
    sched_collector_ = std::thread(&Monitor::sched_collect_loop, this);
    spdlog::info("monitor worker threads started");
    return true;
}

void Monitor::stop() {
    const bool was_running = running_.exchange(false);
    if (was_running) {
        spdlog::info("monitor stopping");
    }
    if (fast_collector_.joinable()) {
        fast_collector_.join();
        spdlog::debug("fast collector thread joined");
    }
    if (sched_collector_.joinable()) {
        sched_collector_.join();
        spdlog::debug("sched collector thread joined");
    }
    metrics_server_->stop();
    dispatcher_->stop();
    if (was_running) {
        spdlog::info("monitor stopped");
    }
}

void Monitor::fast_collect_loop() {
    std::string ignored;
    if (!set_current_thread_affinity(config_.fast_collector_cpu, &ignored) && config_.fast_collector_cpu >= 0) {
        spdlog::warn("failed to set fast collector CPU affinity: cpu={} reason={}", config_.fast_collector_cpu,
                     ignored);
    }
    if (!set_current_thread_nice(config_.fast_collector_nice, &ignored)) {
        spdlog::warn("failed to set fast collector nice: nice={} reason={}", config_.fast_collector_nice, ignored);
    }
    spdlog::info("fast collector thread running: interval_ms={} cpu={} nice={}", config_.fast_collect_interval_ms,
                 config_.fast_collector_cpu, config_.fast_collector_nice);

    const auto interval = std::chrono::milliseconds(config_.fast_collect_interval_ms);
    auto deadline = std::chrono::steady_clock::now() + interval;
    while (running_.load()) {
        const uint64_t start = monotonic_ms();
        const MeminfoSample sample = meminfo_->collect();
        if (!sample.valid) {
            publish_collector_failure(fast_queue_, kMeminfoCollectorId, meminfo_failures_,
                                      last_meminfo_failure_event_ms_);
        } else {
            record_meminfo_metrics(sample);
            if (auto event = fast_rules_->evaluate_memory(sample)) {
                publish_event(fast_queue_, *event);
            }
        }
        const SelfStatusSample self_sample = self_status_->collect();
        if (self_sample.valid) {
            record_self_status_metrics(self_sample);
            if (auto event = fast_rules_->evaluate_self_rss(self_sample.vm_rss_kb)) {
                publish_event(fast_queue_, *event);
            }
        } else {
            publish_collector_failure(fast_queue_, kSelfStatusCollectorId, self_status_failures_,
                                      last_self_status_failure_event_ms_);
        }
        if (auto event = fast_rules_->evaluate_queue(queue_snapshot())) {
            publish_event(fast_queue_, *event);
        }
        const uint64_t elapsed = monotonic_ms() - start;
        metrics_.set_gauge("minilisysm_collector_elapsed_ms", static_cast<double>(elapsed), {{"collector", "fast"}});
        if (elapsed > config_.sched_collector_overrun_warning_ms) {
            spdlog::warn("fast collector overrun: elapsed_ms={} threshold_ms={}", elapsed,
                         config_.sched_collector_overrun_warning_ms);
            metrics_.inc_counter("minilisysm_collector_overruns_total", 1.0, {{"collector", "fast"}});
            InternalEvent overrun;
            overrun.event_type = EventType::MonitorOverrun;
            overrun.level = EventLevel::Warning;
            overrun.value = static_cast<double>(elapsed);
            overrun.warning_threshold = static_cast<double>(config_.sched_collector_overrun_warning_ms);
            publish_event(fast_queue_, overrun);
        }
        std::this_thread::sleep_until(deadline);
        deadline = next_deadline(deadline, interval);
    }
}

void Monitor::sched_collect_loop() {
    std::string ignored;
    if (!set_current_thread_affinity(config_.sched_collector_cpu, &ignored) && config_.sched_collector_cpu >= 0) {
        spdlog::warn("failed to set sched collector CPU affinity: cpu={} reason={}", config_.sched_collector_cpu,
                     ignored);
    }
    if (!set_current_thread_nice(config_.sched_collector_nice, &ignored)) {
        spdlog::warn("failed to set sched collector nice: nice={} reason={}", config_.sched_collector_nice, ignored);
    }
    spdlog::info("sched collector thread running: interval_ms={} cpu={} nice={}", config_.fast_collect_interval_ms,
                 config_.sched_collector_cpu, config_.sched_collector_nice);

    const auto interval = std::chrono::milliseconds(config_.fast_collect_interval_ms);
    auto deadline = std::chrono::steady_clock::now() + interval;
    while (running_.load()) {
        const uint64_t start = monotonic_ms();
        for (const SchedDelaySample& sched_sample : sched_delay_->collect()) {
            record_sched_delay_metrics(sched_sample);
            if (auto event = sched_rules_->evaluate_sched_delay(sched_sample)) {
                publish_event(sched_queue_, *event);
            }
        }
        if (sched_delay_->last_failure_count() > 0) {
            publish_collector_failure(sched_queue_, kSchedDelayCollectorId, sched_delay_failures_,
                                      last_sched_delay_failure_event_ms_);
        }
        for (const IoDelaySample& io_sample : io_delay_->collect()) {
            record_io_delay_metrics(io_sample);
            if (auto event = sched_rules_->evaluate_io_delay(io_sample)) {
                publish_event(sched_queue_, *event);
            }
        }
        if (io_delay_->last_failure_count() > 0) {
            publish_collector_failure(sched_queue_, kIoDelayCollectorId, io_delay_failures_,
                                      last_io_delay_failure_event_ms_);
        }
        const uint64_t elapsed = monotonic_ms() - start;
        metrics_.set_gauge("minilisysm_collector_elapsed_ms", static_cast<double>(elapsed), {{"collector", "sched"}});
        if (elapsed > config_.sched_collector_overrun_warning_ms) {
            spdlog::warn("sched collector overrun: elapsed_ms={} threshold_ms={}", elapsed,
                         config_.sched_collector_overrun_warning_ms);
            metrics_.inc_counter("minilisysm_collector_overruns_total", 1.0, {{"collector", "sched"}});
            InternalEvent overrun;
            overrun.event_type = EventType::MonitorOverrun;
            overrun.level = EventLevel::Warning;
            overrun.value = static_cast<double>(elapsed);
            overrun.warning_threshold = static_cast<double>(config_.sched_collector_overrun_warning_ms);
            publish_event(sched_queue_, overrun);
        }
        std::this_thread::sleep_until(deadline);
        deadline = next_deadline(deadline, interval);
    }
}

bool Monitor::publish_event(SpscRingBuffer<InternalEvent>& queue, const InternalEvent& source) {
    InternalEvent event = source;
    event.sequence = next_sequence_.fetch_add(1);
    event.realtime_ms = realtime_ms();
    event.monotonic_ms = monotonic_ms();
    event.boottime_ms = boottime_ms();
    if (event.last_seen_ms == 0) {
        event.last_seen_ms = event.realtime_ms;
    }
    if (event.first_seen_ms == 0) {
        event.first_seen_ms = event.realtime_ms;
    }
    if (!queue.push(event, event.level)) {
        spdlog::warn("event queue push failed: event_type={} level={} sequence={}", to_string(event.event_type),
                     to_string(event.level), event.sequence);
        return false;
    }
    record_event_metrics(event);
    return true;
}

void Monitor::publish_collector_failure(SpscRingBuffer<InternalEvent>& queue, uint32_t collector_id,
                                        std::atomic<uint64_t>& total_failures, uint64_t& last_event_ms) {
    const uint64_t failures = total_failures.fetch_add(1) + 1;
    const uint64_t now = monotonic_ms();
    if (last_event_ms != 0 && now - last_event_ms < kCollectorFailureEventIntervalMs) {
        return;
    }
    last_event_ms = now;

    InternalEvent event;
    event.event_type = EventType::CollectorFailure;
    event.level = EventLevel::Warning;
    event.status = EventStatus::Active;
    event.value = static_cast<double>(failures);
    event.warning_threshold = 1.0;
    event.continuous_hit_count = 1;
    event.hit_count = failures;
    event.evidence_count = 1;
    set_evidence_key(event.evidence[0], "collector_id");
    event.evidence[0].value = static_cast<double>(collector_id);
    spdlog::warn("collector failure observed: collector_id={} total_failures={}", collector_id, failures);
    publish_event(queue, event);
}

void Monitor::publish_started_event() {
    InternalEvent event;
    event.event_type = EventType::MonitorStarted;
    event.level = EventLevel::Info;
    event.status = EventStatus::Active;
    publish_event(fast_queue_, event);
}

QueueSnapshot Monitor::queue_snapshot() const {
    QueueSnapshot snapshot;
    for (const SpscRingBuffer<InternalEvent>* queue : event_queues_) {
        const QueueStats stats = queue->stats();
        snapshot.push_fail_count += stats.push_fail_count;
        snapshot.dropped_count += stats.dropped_info_count + stats.dropped_warning_count + stats.dropped_critical_count;
        snapshot.dropped_critical_count += stats.dropped_critical_count;
        snapshot.reserve_reject_count += stats.reserve_reject_count;
        snapshot.depth += queue->depth();
        snapshot.capacity += queue->usable_capacity();
        snapshot.high_watermark = std::max(snapshot.high_watermark, stats.high_watermark);
    }
    if (dispatcher_) {
        const DispatcherStats dispatcher_stats = dispatcher_->stats();
        snapshot.dispatcher_sink_push_failures = dispatcher_stats.sink_queue_push_failures;
        snapshot.sink_dropped_count = dispatcher_stats.sink_queue_dropped_events;
        snapshot.sink_dropped_critical_count = dispatcher_stats.sink_queue_dropped_critical_events;
        snapshot.sink_reserve_reject_count = dispatcher_stats.sink_queue_reserve_reject_events;
        snapshot.sink_depth = dispatcher_stats.sink_queue_depth;
        snapshot.sink_capacity = dispatcher_stats.sink_queue_capacity;
        snapshot.sink_high_watermark = dispatcher_stats.sink_queue_high_watermark;
    }
    return snapshot;
}

void Monitor::record_event_metrics(const InternalEvent& event) {
    event_type_counts_[event_type_index(event.event_type)].fetch_add(1, std::memory_order_relaxed);
    event_level_counts_[event_level_index(event.level)].fetch_add(1, std::memory_order_relaxed);
    if (config_.metrics_scrape_runtime) {
        metrics_.inc_counter("minilisysm_events_published_total");
        metrics_.inc_counter("minilisysm_events_by_type_total", 1.0, {{"type", to_string(event.event_type)}});
        metrics_.inc_counter("minilisysm_events_by_level_total", 1.0, {{"level", to_string(event.level)}});
    }
}

std::string Monitor::render_metrics() const {
    const QueueSnapshot queues = queue_snapshot();
    if (config_.metrics_scrape_runtime) {
        metrics_.set_gauge("minilisysm_up", running_.load() ? 1.0 : 0.0);
        metrics_.set_counter("minilisysm_events_published_total", static_cast<double>(next_sequence_.load() - 1));
        metrics_.set_gauge("minilisysm_queue_depth", static_cast<double>(queues.depth), {{"queue", "source"}});
        metrics_.set_gauge("minilisysm_queue_depth", static_cast<double>(queues.sink_depth), {{"queue", "sink"}});
        metrics_.set_gauge("minilisysm_queue_capacity", static_cast<double>(queues.capacity), {{"queue", "source"}});
        metrics_.set_gauge("minilisysm_queue_capacity", static_cast<double>(queues.sink_capacity), {{"queue", "sink"}});
        metrics_.set_counter("minilisysm_queue_dropped_total", static_cast<double>(queues.dropped_count),
                             {{"queue", "source"}});
        metrics_.set_counter("minilisysm_queue_dropped_total", static_cast<double>(queues.sink_dropped_count),
                             {{"queue", "sink"}});
        metrics_.set_gauge("minilisysm_queue_high_watermark", static_cast<double>(queues.high_watermark),
                           {{"queue", "source"}});
        metrics_.set_gauge("minilisysm_queue_high_watermark", static_cast<double>(queues.sink_high_watermark),
                           {{"queue", "sink"}});
        metrics_.set_counter("minilisysm_dispatcher_sink_push_failures_total",
                             static_cast<double>(queues.dispatcher_sink_push_failures));
        if (dispatcher_) {
            for (const auto& item : dispatcher_->sink_stats()) {
                const std::vector<MetricLabel> label{{"sink", item.first}};
                const SinkStats& stats = item.second;
                metrics_.set_gauge("minilisysm_sink_queue_depth", static_cast<double>(stats.queue_depth), label);
                metrics_.set_gauge("minilisysm_sink_queue_capacity", static_cast<double>(stats.queue_capacity), label);
                metrics_.set_counter("minilisysm_sink_dropped_total", static_cast<double>(stats.dropped_events), label);
                metrics_.set_counter("minilisysm_network_sent_total", static_cast<double>(stats.sent_events), label);
                metrics_.set_counter("minilisysm_network_send_errors_total", static_cast<double>(stats.send_errors),
                                     label);
                metrics_.set_counter("minilisysm_network_retries_total", static_cast<double>(stats.retry_count), label);
                metrics_.set_gauge("minilisysm_network_wal_pending_events",
                                   static_cast<double>(stats.wal_pending_events), label);
                metrics_.set_gauge("minilisysm_network_wal_bytes", static_cast<double>(stats.wal_bytes), label);
                metrics_.set_counter("minilisysm_network_wal_overflow_dropped_total",
                                     static_cast<double>(stats.wal_overflow_dropped_events), label);
            }
        }
    }
    if (config_.metrics_scrape_collectors) {
        metrics_.set_counter("minilisysm_collector_failures_total", static_cast<double>(meminfo_failures_.load()),
                             {{"collector", "meminfo"}});
        metrics_.set_counter("minilisysm_collector_failures_total", static_cast<double>(self_status_failures_.load()),
                             {{"collector", "self_status"}});
        metrics_.set_counter("minilisysm_collector_failures_total", static_cast<double>(sched_delay_failures_.load()),
                             {{"collector", "sched_delay"}});
        metrics_.set_counter("minilisysm_collector_failures_total", static_cast<double>(io_delay_failures_.load()),
                             {{"collector", "io_delay"}});
        const SchedDelayCollectorRuntimeStats sched_stats = sched_delay_->runtime_stats();
        metrics_.set_counter("minilisysm_ebpf_ringbuf_drops_total",
                             static_cast<double>(sched_stats.ebpf_ringbuf_drops));
        metrics_.set_counter("minilisysm_ebpf_allowlist_exec_seen_total",
                             static_cast<double>(sched_stats.ebpf_allowlist_exec_seen));
        metrics_.set_counter("minilisysm_ebpf_allowlist_exit_cleaned_total",
                             static_cast<double>(sched_stats.ebpf_allowlist_exit_cleaned));
        metrics_.set_counter("minilisysm_ebpf_allowlist_stale_hits_total",
                             static_cast<double>(sched_stats.ebpf_allowlist_stale_hits));
        metrics_.set_counter("minilisysm_ebpf_aggregate_drops_total",
                             static_cast<double>(sched_stats.ebpf_aggregate_drops));
        metrics_.set_gauge("minilisysm_ebpf_allowlist_scanned_processes",
                           static_cast<double>(sched_stats.allowlist_scanned_processes));
        metrics_.set_gauge("minilisysm_ebpf_allowlist_matched_pids",
                           static_cast<double>(sched_stats.allowlist_matched_pids));
        metrics_.set_gauge("minilisysm_ebpf_allowlist_matched_tids",
                           static_cast<double>(sched_stats.allowlist_matched_tids));
        metrics_.set_gauge("minilisysm_ebpf_allowlist_refresh_elapsed_ms",
                           static_cast<double>(sched_stats.allowlist_refresh_elapsed_ms));
    }
    return metrics_.render_prometheus();
}

void Monitor::record_meminfo_metrics(const MeminfoSample& sample) {
    if (!config_.metrics_scrape_collectors || !sample.valid) {
        return;
    }
    metrics_.set_gauge("minilisysm_system_memory_total_bytes", static_cast<double>(sample.mem_total_kb) * 1024.0);
    metrics_.set_gauge("minilisysm_system_memory_available_bytes",
                       static_cast<double>(sample.mem_available_kb) * 1024.0);
}

void Monitor::record_self_status_metrics(const SelfStatusSample& sample) {
    if (!config_.metrics_scrape_collectors || !sample.valid) {
        return;
    }
    metrics_.set_gauge("minilisysm_monitor_rss_bytes", static_cast<double>(sample.vm_rss_kb) * 1024.0);
}

void Monitor::record_sched_delay_metrics(const SchedDelaySample& sample) {
    if (!config_.metrics_scrape_collectors || !sample.valid) {
        return;
    }
    const std::string pid = std::to_string(sample.pid);
    const std::string tid = std::to_string(sample.tid);
    metrics_.set_gauge("minilisysm_sched_wait_us", static_cast<double>(sample.delta_wait_sum_us),
                       {{"pid", pid}, {"tid", tid}});
    metrics_.set_gauge("minilisysm_sched_involuntary_switches", static_cast<double>(sample.delta_involuntary_switches),
                       {{"pid", pid}, {"tid", tid}});
    metrics_.set_gauge("minilisysm_sched_max_wait_us", static_cast<double>(sample.max_wait_us),
                       {{"pid", pid}, {"tid", tid}});
    metrics_.set_gauge("minilisysm_sched_avg_wait_us", static_cast<double>(sample.avg_wait_us),
                       {{"pid", pid}, {"tid", tid}});
    metrics_.set_gauge("minilisysm_sched_aggregate_count", static_cast<double>(sample.aggregate_count),
                       {{"pid", pid}, {"tid", tid}});
}

void Monitor::record_io_delay_metrics(const IoDelaySample& sample) {
    if (!config_.metrics_scrape_collectors || !sample.valid) {
        return;
    }
    metrics_.set_gauge("minilisysm_io_await_ms", sample.avg_await_ms, {{"device", sample.device}});
    metrics_.set_gauge("minilisysm_io_util_percent", sample.util_percent, {{"device", sample.device}});
    metrics_.set_gauge("minilisysm_io_delta_count", static_cast<double>(sample.delta_io_count),
                       {{"device", sample.device}});
}

} // namespace lisysm
