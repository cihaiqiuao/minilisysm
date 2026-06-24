#include "lisysm/runtime/monitor.hpp"
#include "lisysm/collectors/collector_factory.hpp"
#include "lisysm/collectors/meminfo_collector.hpp"
#include "lisysm/collectors/sched_delay_collector.hpp"
#include "lisysm/collectors/self_status_collector.hpp"
#include "lisysm/core/time.hpp"
#include "lisysm/rules/rule_factory.hpp"
#include "lisysm/runtime/thread_policy.hpp"
#include "lisysm/storage/event_store.hpp"
#include "lisysm/storage/storage_factory.hpp"

#include <chrono>
#include <cstring>
#include <iostream>
#include <utility>

namespace lisysm {
namespace {

constexpr uint32_t kMeminfoCollectorId = 1;
constexpr uint32_t kSelfStatusCollectorId = 2;
constexpr uint32_t kSchedDelayCollectorId = 3;
constexpr uint64_t kCollectorFailureEventIntervalMs = 60000;

void set_evidence_key(EvidenceItem& item, const char* key)
{
    std::strncpy(item.key.data(), key, kEvidenceKeySize - 1);
    item.key[kEvidenceKeySize - 1] = '\0';
}

} // namespace

Monitor::Monitor(MonitorConfig config)
    : config_(std::move(config)),
      fast_queue_(config_.event_queue_capacity),
      sched_queue_(config_.event_queue_capacity),
      event_queues_{&fast_queue_, &sched_queue_},
      store_(StorageFactory::create_event_store(config_, event_queues_)),
      meminfo_(CollectorFactory::create_meminfo_collector()),
      self_status_(CollectorFactory::create_self_status_collector()),
      sched_delay_(CollectorFactory::create_sched_delay_collector(config_)),
      fast_rules_(RuleFactory::create_fast_rule_engine(config_)),
      sched_rules_(RuleFactory::create_sched_rule_engine(config_))
{
}

Monitor::~Monitor()
{
    stop();
}

bool Monitor::start()
{
    if (!config_.enable) {
        return false;
    }
    if (!store_->start()) {
        return false;
    }
    running_.store(true);
    publish_started_event();
    fast_collector_ = std::thread(&Monitor::fast_collect_loop, this);
    sched_collector_ = std::thread(&Monitor::sched_collect_loop, this);
    return true;
}

void Monitor::stop()
{
    running_.store(false);
    if (fast_collector_.joinable()) {
        fast_collector_.join();
    }
    if (sched_collector_.joinable()) {
        sched_collector_.join();
    }
    store_->stop();
}

void Monitor::fast_collect_loop()
{
    std::string ignored;
    set_current_thread_affinity(config_.fast_collector_cpu, &ignored);
    set_current_thread_nice(config_.fast_collector_nice, &ignored);

    const auto interval = std::chrono::milliseconds(config_.fast_collect_interval_ms);
    while (running_.load()) {
        const uint64_t start = monotonic_ms();
        const MeminfoSample sample = meminfo_->collect();
        if (!sample.valid) {
            publish_collector_failure(
                fast_queue_,
                kMeminfoCollectorId,
                meminfo_failures_,
                last_meminfo_failure_event_ms_);
        } else {
            if (auto event = fast_rules_->evaluate_memory(sample)) {
                publish_event(fast_queue_, *event);
            }
        }
        const SelfStatusSample self_sample = self_status_->collect();
        if (self_sample.valid) {
            if (auto event = fast_rules_->evaluate_self_rss(self_sample.vm_rss_kb)) {
                publish_event(fast_queue_, *event);
            }
        } else {
            publish_collector_failure(
                fast_queue_,
                kSelfStatusCollectorId,
                self_status_failures_,
                last_self_status_failure_event_ms_);
        }
        if (auto event = fast_rules_->evaluate_queue(queue_snapshot())) {
            publish_event(fast_queue_, *event);
        }
        const uint64_t elapsed = monotonic_ms() - start;
        if (elapsed > 200) {
            InternalEvent overrun;
            overrun.event_type = EventType::MonitorOverrun;
            overrun.level = EventLevel::Warning;
            overrun.value = static_cast<double>(elapsed);
            overrun.warning_threshold = 200.0;
            publish_event(fast_queue_, overrun);
        }
        std::this_thread::sleep_for(interval);
    }
}

void Monitor::sched_collect_loop()
{
    std::string ignored;
    set_current_thread_affinity(config_.sched_collector_cpu, &ignored);
    set_current_thread_nice(config_.sched_collector_nice, &ignored);

    const auto interval = std::chrono::milliseconds(config_.fast_collect_interval_ms);
    while (running_.load()) {
        const uint64_t start = monotonic_ms();
        for (const SchedDelaySample& sched_sample : sched_delay_->collect()) {
            if (auto event = sched_rules_->evaluate_sched_delay(sched_sample)) {
                publish_event(sched_queue_, *event);
            }
        }
        if (sched_delay_->last_failure_count() > 0) {
            publish_collector_failure(
                sched_queue_,
                kSchedDelayCollectorId,
                sched_delay_failures_,
                last_sched_delay_failure_event_ms_);
        }
        const uint64_t elapsed = monotonic_ms() - start;
        if (elapsed > 200) {
            InternalEvent overrun;
            overrun.event_type = EventType::MonitorOverrun;
            overrun.level = EventLevel::Warning;
            overrun.value = static_cast<double>(elapsed);
            overrun.warning_threshold = 200.0;
            publish_event(sched_queue_, overrun);
        }
        std::this_thread::sleep_for(interval);
    }
}

bool Monitor::publish_event(SpscRingBuffer<InternalEvent>& queue, const InternalEvent& source)
{
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
    return queue.push(event, event.level);
}

void Monitor::publish_collector_failure(
    SpscRingBuffer<InternalEvent>& queue,
    uint32_t collector_id,
    uint64_t& total_failures,
    uint64_t& last_event_ms)
{
    ++total_failures;
    const uint64_t now = monotonic_ms();
    if (last_event_ms != 0 && now - last_event_ms < kCollectorFailureEventIntervalMs) {
        return;
    }
    last_event_ms = now;

    InternalEvent event;
    event.event_type = EventType::CollectorFailure;
    event.level = EventLevel::Warning;
    event.status = EventStatus::Active;
    event.value = static_cast<double>(total_failures);
    event.warning_threshold = 1.0;
    event.continuous_hit_count = 1;
    event.hit_count = total_failures;
    event.evidence_count = 1;
    set_evidence_key(event.evidence[0], "collector_id");
    event.evidence[0].value = static_cast<double>(collector_id);
    publish_event(queue, event);
}

void Monitor::publish_started_event()
{
    InternalEvent event;
    event.event_type = EventType::MonitorStarted;
    event.level = EventLevel::Info;
    event.status = EventStatus::Active;
    publish_event(fast_queue_, event);
}

QueueSnapshot Monitor::queue_snapshot() const
{
    QueueSnapshot snapshot;
    for (const SpscRingBuffer<InternalEvent>* queue : event_queues_) {
        const QueueStats stats = queue->stats();
        snapshot.push_fail_count += stats.push_fail_count;
        snapshot.dropped_count +=
            stats.dropped_info_count + stats.dropped_warning_count + stats.dropped_critical_count;
        snapshot.depth += queue->depth();
        snapshot.capacity += queue->usable_capacity();
    }
    return snapshot;
}

} // namespace lisysm
