#include "minilisysm/runtime/monitor.hpp"
#include "minilisysm/collectors/collector_factory.hpp"
#include "minilisysm/collectors/cpu_usage_collector.hpp"
#include "minilisysm/collectors/hardware_health_collector.hpp"
#include "minilisysm/collectors/io_delay_collector.hpp"
#include "minilisysm/collectors/meminfo_collector.hpp"
#include "minilisysm/collectors/self_status_collector.hpp"
#include "minilisysm/core/time.hpp"
#include "minilisysm/rules/rule_factory.hpp"
#include "minilisysm/runtime/event_dispatcher.hpp"
#include "minilisysm/runtime/metrics_server.hpp"
#include "minilisysm/runtime/thread_policy.hpp"
#include "minilisysm/storage/storage_factory.hpp"
#include "whitelisted_process_reader.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <unordered_set>
#include <unistd.h>
#include <utility>

namespace lisysm {
namespace {

constexpr uint32_t kMeminfoCollectorId = 1;
constexpr uint32_t kSelfStatusCollectorId = 2;
constexpr uint32_t kSchedDelayCollectorId = 3;
constexpr uint32_t kIoDelayCollectorId = 4;
constexpr uint32_t kCpuUsageCollectorId = 5;
constexpr uint64_t kCollectorFailureEventIntervalMs = 60000;

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
    : config_(std::move(config)), metrics_(config_), fast_queue_(config_.event_queue_capacity, config_.critical_reserved_slots,
                                              config_.drop_info_when_full, config_.drop_warning_when_full),
      sched_queue_(config_.event_queue_capacity, config_.critical_reserved_slots, config_.drop_info_when_full,
                   config_.drop_warning_when_full),
      event_queues_{&fast_queue_, &sched_queue_},
      dispatcher_(
          std::make_unique<EventDispatcherGroup>(config_, event_queues_, StorageFactory::create_event_sinks(config_))),
      meminfo_(CollectorFactory::create_meminfo_collector()),
      cpu_usage_(CollectorFactory::create_cpu_usage_collector(config_)),
      self_status_(CollectorFactory::create_self_status_collector()),
      hardware_health_(CollectorFactory::create_hardware_health_collector()),
      sched_delay_(CollectorFactory::create_sched_delay_collector(config_)),
      io_delay_(CollectorFactory::create_io_delay_collector(config_)),
      metrics_server_(std::make_unique<MetricsServer>(config_, [this]() { return render_metrics(); })),
      fast_rules_(RuleFactory::create_fast_rule_engine(config_)),
      sched_rules_(RuleFactory::create_sched_rule_engine(config_)) {}

Monitor::~Monitor() {
    stop();
}

bool Monitor::start() {
    std::lock_guard<std::mutex> lifecycle_lock(lifecycle_mutex_);
    if (running_.load()) {
        spdlog::warn("monitor start skipped: already running");
        return false;
    }
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
    std::lock_guard<std::mutex> lifecycle_lock(lifecycle_mutex_);
    const bool was_running = running_.exchange(false);
    if (!was_running) {
        return;
    }
    spdlog::info("monitor stopping");
    stop_condition_.notify_all();
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
    spdlog::info("monitor stopped");
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
            metrics_.record_meminfo(sample);
            if (auto event = fast_rules_->evaluate_memory(sample)) {
                publish_event(fast_queue_, *event);
            }
        }
        const std::vector<CpuUsageSample> cpu_samples = cpu_usage_->collect();
        for (const CpuUsageSample& cpu_sample : cpu_samples) {
            metrics_.record_cpu_usage(cpu_sample);
            if (auto event = fast_rules_->evaluate_cpu_usage(cpu_sample)) {
                publish_event(fast_queue_, *event);
            }
        }
        if (cpu_samples.empty() && cpu_usage_->last_failure_count() > 0) {
            publish_collector_failure(fast_queue_, kCpuUsageCollectorId, cpu_usage_failures_,
                                      last_cpu_usage_failure_event_ms_);
        }
        const SelfStatusSample self_sample = self_status_->collect();
        if (self_sample.valid) {
            metrics_.record_self_status(self_sample);
            if (auto event = fast_rules_->evaluate_self_rss(self_sample.vm_rss_kb)) {
                publish_event(fast_queue_, *event);
            }
        } else {
            publish_collector_failure(fast_queue_, kSelfStatusCollectorId, self_status_failures_,
                                      last_self_status_failure_event_ms_);
        }
        record_whitelisted_process_metrics();
        if (auto event = fast_rules_->evaluate_queue(queue_snapshot())) {
            publish_event(fast_queue_, *event);
        }
        const uint64_t elapsed = monotonic_ms() - start;
        metrics_.record_collector_elapsed("fast", elapsed);
        if (elapsed > config_.sched_collector_overrun_warning_ms) {
            spdlog::warn("fast collector overrun: elapsed_ms={} threshold_ms={}", elapsed,
                         config_.sched_collector_overrun_warning_ms);
            metrics_.record_collector_overrun("fast");
            InternalEvent overrun;
            overrun.event_type = EventType::MonitorOverrun;
            overrun.level = EventLevel::Warning;
            overrun.value = static_cast<double>(elapsed);
            overrun.warning_threshold = static_cast<double>(config_.sched_collector_overrun_warning_ms);
            publish_event(fast_queue_, overrun);
        }
        {
            std::unique_lock<std::mutex> lock(stop_mutex_);
            if (stop_condition_.wait_until(lock, deadline, [this]() { return !running_.load(); })) {
                break;
            }
        }
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
    spdlog::info("sched collector thread running: interval_ms={} cpu={} nice={}", config_.low_freq_collect_interval_ms,
                 config_.sched_collector_cpu, config_.sched_collector_nice);

    const auto interval = std::chrono::milliseconds(config_.low_freq_collect_interval_ms);
    auto deadline = std::chrono::steady_clock::now() + interval;
    while (running_.load()) {
        const uint64_t start = monotonic_ms();
        const std::vector<SchedDelaySample> sched_samples = sched_delay_->collect();
        metrics_.record_sched_delay(sched_samples);
        for (const SchedDelaySample& sched_sample : sched_samples) {
            if (auto event = sched_rules_->evaluate_sched_delay(sched_sample)) {
                publish_event(sched_queue_, *event);
            }
        }
        if (sched_delay_->last_failure_count() > 0) {
            publish_collector_failure(sched_queue_, kSchedDelayCollectorId, sched_delay_failures_,
                                      last_sched_delay_failure_event_ms_);
        }
        for (const IoDelaySample& io_sample : io_delay_->collect()) {
            metrics_.record_io_delay(io_sample);
            if (auto event = sched_rules_->evaluate_io_delay(io_sample)) {
                publish_event(sched_queue_, *event);
            }
        }
        if (io_delay_->last_failure_count() > 0) {
            publish_collector_failure(sched_queue_, kIoDelayCollectorId, io_delay_failures_,
                                      last_io_delay_failure_event_ms_);
        }
        metrics_.record_hardware_health(hardware_health_->collect());
        const uint64_t elapsed = monotonic_ms() - start;
        metrics_.record_collector_elapsed("sched", elapsed);
        if (elapsed > config_.sched_collector_overrun_warning_ms) {
            spdlog::warn("sched collector overrun: elapsed_ms={} threshold_ms={}", elapsed,
                         config_.sched_collector_overrun_warning_ms);
            metrics_.record_collector_overrun("sched");
            InternalEvent overrun;
            overrun.event_type = EventType::MonitorOverrun;
            overrun.level = EventLevel::Warning;
            overrun.value = static_cast<double>(elapsed);
            overrun.warning_threshold = static_cast<double>(config_.sched_collector_overrun_warning_ms);
            publish_event(sched_queue_, overrun);
        }
        {
            std::unique_lock<std::mutex> lock(stop_mutex_);
            if (stop_condition_.wait_until(lock, deadline, [this]() { return !running_.load(); })) {
                break;
            }
        }
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
    metrics_.record_event(event);
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

std::string Monitor::render_metrics() const {
    const QueueSnapshot queues = queue_snapshot();
    const std::vector<std::pair<std::string, SinkStats>> sink_stats = dispatcher_ ? dispatcher_->sink_stats()
                                                                                  : std::vector<std::pair<std::string, SinkStats>>{};
    return metrics_.render(running_.load(), next_sequence_.load(), queues, sink_stats, meminfo_failures_.load(),
                           cpu_usage_failures_.load(), self_status_failures_.load(), sched_delay_failures_.load(),
                           io_delay_failures_.load(), sched_delay_->runtime_stats());
}

void Monitor::record_whitelisted_process_metrics() {
    if (config_.sched_process_whitelist.empty() ||
        (!config_.metrics_scrape_collectors && !config_.process_memory_enable)) {
        return;
    }
    for (const std::string& name : config_.sched_process_whitelist) {
        metrics_.record_whitelisted_process_status(name, false);
    }
    const long clock_ticks = ::sysconf(_SC_CLK_TCK);
    const auto now = std::chrono::steady_clock::now();
    const detail::WhitelistedProcessScan scan =
        detail::scan_whitelisted_processes("/proc", config_.sched_process_whitelist);
    std::unordered_set<int32_t> live_pids(scan.uncertain_pids.begin(), scan.uncertain_pids.end());
    for (const detail::WhitelistedProcessSample& sample : scan.samples) {
        live_pids.insert(sample.pid);
        auto instance = process_instances_.find(sample.pid);
        if (instance == process_instances_.end()) {
            process_instances_.emplace(sample.pid, ProcessInstance{sample.name, sample.starttime_ticks});
        } else if (instance->second.starttime_ticks != sample.starttime_ticks || instance->second.name != sample.name) {
            fast_rules_->forget_process_memory(instance->second.name, sample.pid);
            process_cpu_baselines_.erase(sample.pid);
            process_memory_history_.erase(sample.pid);
            instance->second = ProcessInstance{sample.name, sample.starttime_ticks};
        }

        metrics_.record_whitelisted_process_status(sample.name, true);
        double cpu_percent = 0.0;
        const auto previous = process_cpu_baselines_.find(sample.pid);
        if (previous != process_cpu_baselines_.end() && clock_ticks > 0) {
            const double elapsed = std::chrono::duration<double>(now - previous->second.sampled_at).count();
            if (elapsed > 0.0 && sample.cpu_ticks >= previous->second.ticks) {
                cpu_percent = static_cast<double>(sample.cpu_ticks - previous->second.ticks) * 100.0 /
                              (static_cast<double>(clock_ticks) * elapsed);
            }
        }
        process_cpu_baselines_[sample.pid] = ProcessCpuBaseline{sample.cpu_ticks, now};
        metrics_.record_whitelisted_process_sample(sample.name, sample.pid, sample.rss_bytes, sample.threads,
                                                   cpu_percent);
        if (config_.process_memory_enable) {
            auto& history = process_memory_history_[sample.pid];
            history.push_back(ProcessMemorySample{now, sample.rss_bytes});
            const auto window = std::chrono::seconds(config_.process_memory_growth_window_sec);
            while (history.size() > 1 && now - history[1].sampled_at >= window) {
                history.pop_front();
            }
            if (history.size() > 1 && now - history.front().sampled_at >= window) {
                const double growth_mb =
                    (static_cast<double>(sample.rss_bytes) - static_cast<double>(history.front().rss_bytes)) / 1048576.0;
                if (auto event = fast_rules_->evaluate_process_memory_growth(sample.name, sample.pid, growth_mb)) {
                    publish_event(fast_queue_, *event);
                }
            }
        }
    }
    if (scan.complete) {
        for (auto instance = process_instances_.begin(); instance != process_instances_.end();) {
            if (live_pids.find(instance->first) == live_pids.end()) {
                fast_rules_->forget_process_memory(instance->second.name, instance->first);
                process_cpu_baselines_.erase(instance->first);
                process_memory_history_.erase(instance->first);
                instance = process_instances_.erase(instance);
            } else {
                ++instance;
            }
        }
    }
}

} // namespace lisysm
