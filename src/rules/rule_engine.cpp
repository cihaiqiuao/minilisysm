#include "lisysm/rules/rule_engine.hpp"
#include "lisysm/core/time.hpp"

#include <algorithm>
#include <cstring>

namespace lisysm {
namespace {

uint64_t sched_key(int32_t pid, int32_t tid)
{
    return (static_cast<uint64_t>(static_cast<uint32_t>(pid)) << 32U) |
           static_cast<uint32_t>(tid);
}

void set_evidence_key(EvidenceItem& item, const char* key)
{
    std::strncpy(item.key.data(), key, kEvidenceKeySize - 1);
    item.key[kEvidenceKeySize - 1] = '\0';
}

} // namespace

RuleEngine::RuleEngine(const MonitorConfig& config) : config_(config)
{
    if (!config_.memory_rule_enable) {
        memory_.state = RuleState::Disabled;
    }
    if (!config_.self_protection_enable) {
        self_rss_.state = RuleState::Disabled;
        queue_.state = RuleState::Disabled;
    }
}

std::optional<InternalEvent> RuleEngine::evaluate_memory(const MeminfoSample& sample)
{
    if (memory_.state == RuleState::Disabled || !sample.valid) {
        return std::nullopt;
    }

    const double available_mb = static_cast<double>(sample.mem_available_kb) / 1024.0;
    memory_.max_value = std::max(memory_.max_value, available_mb);

    if (available_mb <= static_cast<double>(config_.mem_available_critical_mb)) {
        ++memory_.hit_count;
        ++memory_.critical_hit_count;
        ++memory_.total_hit_count;
        memory_.recovery_count = 0;
        if (memory_.hit_count >= config_.continuous_critical_windows &&
            memory_.critical_hit_count >= config_.continuous_critical_windows &&
            memory_.state != RuleState::Critical) {
            memory_.state = RuleState::Critical;
            return make_memory_event(EventLevel::Critical, EventStatus::Active, available_mb);
        }
        return std::nullopt;
    }

    if (available_mb <= static_cast<double>(config_.mem_available_warning_mb)) {
        ++memory_.hit_count;
        ++memory_.total_hit_count;
        memory_.critical_hit_count = 0;
        memory_.recovery_count = 0;
        if (memory_.hit_count >= config_.continuous_warning_windows &&
            memory_.state == RuleState::Normal) {
            memory_.state = RuleState::Warning;
            return make_memory_event(EventLevel::Warning, EventStatus::Active, available_mb);
        }
        return std::nullopt;
    }

    if (available_mb >= static_cast<double>(config_.mem_available_recovery_mb) &&
        memory_.state != RuleState::Normal) {
        ++memory_.recovery_count;
        if (memory_.recovery_count >= config_.recovery_windows) {
            memory_.state = RuleState::Normal;
            memory_.hit_count = 0;
            memory_.critical_hit_count = 0;
            return make_memory_event(EventLevel::Recovery, EventStatus::Resolved, available_mb);
        }
        return std::nullopt;
    }

    memory_.hit_count = 0;
    memory_.critical_hit_count = 0;
    memory_.recovery_count = 0;
    return std::nullopt;
}

std::optional<InternalEvent> RuleEngine::evaluate_self_rss(uint64_t rss_kb)
{
    if (self_rss_.state == RuleState::Disabled || rss_kb == 0) {
        return std::nullopt;
    }
    const double rss_mb = static_cast<double>(rss_kb) / 1024.0;
    self_rss_.max_value = std::max(self_rss_.max_value, rss_mb);

    if (rss_mb >= static_cast<double>(config_.self_rss_hard_limit_mb)) {
        ++self_rss_.hit_count;
        ++self_rss_.critical_hit_count;
        ++self_rss_.total_hit_count;
        self_rss_.recovery_count = 0;
        if (self_rss_.critical_hit_count >= config_.continuous_critical_windows &&
            self_rss_.state != RuleState::Critical) {
            self_rss_.state = RuleState::Critical;
            return make_self_rss_event(EventLevel::Critical, EventStatus::Active, rss_mb);
        }
        return std::nullopt;
    }
    if (rss_mb >= static_cast<double>(config_.self_rss_soft_limit_mb)) {
        ++self_rss_.hit_count;
        ++self_rss_.total_hit_count;
        self_rss_.critical_hit_count = 0;
        self_rss_.recovery_count = 0;
        if (self_rss_.hit_count >= config_.continuous_warning_windows &&
            self_rss_.state == RuleState::Normal) {
            self_rss_.state = RuleState::Warning;
            return make_self_rss_event(EventLevel::Warning, EventStatus::Active, rss_mb);
        }
        return std::nullopt;
    }
    if (rss_mb <= static_cast<double>(config_.self_rss_recovery_mb) &&
        self_rss_.state != RuleState::Normal) {
        ++self_rss_.recovery_count;
        if (self_rss_.recovery_count >= config_.self_recovery_windows) {
            self_rss_.state = RuleState::Normal;
            self_rss_.hit_count = 0;
            self_rss_.critical_hit_count = 0;
            return make_self_rss_event(EventLevel::Recovery, EventStatus::Resolved, rss_mb);
        }
        return std::nullopt;
    }
    self_rss_.hit_count = 0;
    self_rss_.critical_hit_count = 0;
    self_rss_.recovery_count = 0;
    return std::nullopt;
}

std::optional<InternalEvent> RuleEngine::evaluate_queue(const QueueSnapshot& snapshot)
{
    if (queue_.state == RuleState::Disabled || snapshot.capacity == 0) {
        return std::nullopt;
    }
    const double queue_percent =
        static_cast<double>(snapshot.depth) * 100.0 / static_cast<double>(snapshot.capacity);
    queue_.max_value = std::max(queue_.max_value, queue_percent);

    if (queue_percent >= static_cast<double>(config_.queue_critical_percent)) {
        ++queue_.hit_count;
        ++queue_.critical_hit_count;
        ++queue_.total_hit_count;
        queue_.recovery_count = 0;
        if (queue_.critical_hit_count >= config_.continuous_critical_windows &&
            queue_.state != RuleState::Critical) {
            queue_.state = RuleState::Critical;
            return make_queue_event(EventLevel::Critical, EventStatus::Active, queue_percent);
        }
        return std::nullopt;
    }
    if (queue_percent >= static_cast<double>(config_.queue_warning_percent) ||
        snapshot.dropped_count > 0) {
        ++queue_.hit_count;
        ++queue_.total_hit_count;
        queue_.critical_hit_count = 0;
        queue_.recovery_count = 0;
        if (queue_.hit_count >= config_.continuous_warning_windows &&
            queue_.state == RuleState::Normal) {
            queue_.state = RuleState::Warning;
            return make_queue_event(EventLevel::Warning, EventStatus::Active, queue_percent);
        }
        return std::nullopt;
    }
    if (queue_percent <= static_cast<double>(config_.queue_recovery_percent) &&
        queue_.state != RuleState::Normal) {
        ++queue_.recovery_count;
        if (queue_.recovery_count >= config_.self_recovery_windows) {
            queue_.state = RuleState::Normal;
            queue_.hit_count = 0;
            queue_.critical_hit_count = 0;
            return make_queue_event(EventLevel::Recovery, EventStatus::Resolved, queue_percent);
        }
        return std::nullopt;
    }
    queue_.hit_count = 0;
    queue_.critical_hit_count = 0;
    queue_.recovery_count = 0;
    return std::nullopt;
}

std::optional<InternalEvent> RuleEngine::evaluate_sched_delay(const SchedDelaySample& sample)
{
    if (!config_.sched_delay_enable || !sample.valid) {
        return std::nullopt;
    }
    RuleContext& context = sched_delay_[sched_key(sample.pid, sample.tid)];
    const double wait_us = static_cast<double>(sample.delta_wait_sum_us);
    context.max_value = std::max(context.max_value, wait_us);
    const bool switch_hit =
        sample.delta_involuntary_switches >= config_.sched_involuntary_switch_warning;

    if (wait_us >= static_cast<double>(config_.sched_wait_sum_critical_us) && switch_hit) {
        ++context.hit_count;
        ++context.critical_hit_count;
        ++context.total_hit_count;
        context.recovery_count = 0;
        if (context.critical_hit_count >= config_.sched_continuous_critical_windows &&
            context.state != RuleState::Critical) {
            context.state = RuleState::Critical;
            return make_sched_delay_event(sample, EventLevel::Critical, EventStatus::Active, wait_us, context);
        }
        return std::nullopt;
    }
    if (wait_us >= static_cast<double>(config_.sched_wait_sum_warning_us) && switch_hit) {
        ++context.hit_count;
        ++context.total_hit_count;
        context.critical_hit_count = 0;
        context.recovery_count = 0;
        if (context.hit_count >= config_.sched_continuous_warning_windows &&
            context.state == RuleState::Normal) {
            context.state = RuleState::Warning;
            return make_sched_delay_event(sample, EventLevel::Warning, EventStatus::Active, wait_us, context);
        }
        return std::nullopt;
    }
    if (wait_us <= static_cast<double>(config_.sched_wait_sum_recovery_us) &&
        context.state != RuleState::Normal) {
        ++context.recovery_count;
        if (context.recovery_count >= config_.sched_recovery_windows) {
            context.state = RuleState::Normal;
            context.hit_count = 0;
            context.critical_hit_count = 0;
            return make_sched_delay_event(sample, EventLevel::Recovery, EventStatus::Resolved, wait_us, context);
        }
        return std::nullopt;
    }
    context.hit_count = 0;
    context.critical_hit_count = 0;
    context.recovery_count = 0;
    return std::nullopt;
}

InternalEvent RuleEngine::make_memory_event(EventLevel level, EventStatus status, double value_mb) const
{
    InternalEvent event;
    event.rule_id = static_cast<uint32_t>(RuleId::MemoryPressure);
    event.event_type = EventType::MemoryPressure;
    event.level = level;
    event.status = status;
    event.value = value_mb;
    event.warning_threshold = static_cast<double>(config_.mem_available_warning_mb);
    event.critical_threshold = static_cast<double>(config_.mem_available_critical_mb);
    event.window_sec = config_.fast_collect_interval_ms * config_.continuous_warning_windows / 1000;
    event.continuous_hit_count = memory_.hit_count;
    event.hit_count = memory_.total_hit_count;
    event.evidence_count = 2;
    set_evidence_key(event.evidence[0], "recovery_threshold_mb");
    event.evidence[0].value = static_cast<double>(config_.mem_available_recovery_mb);
    set_evidence_key(event.evidence[1], "max_observed_available_mb");
    event.evidence[1].value = memory_.max_value;
    return event;
}

InternalEvent RuleEngine::make_self_rss_event(EventLevel level, EventStatus status, double rss_mb) const
{
    InternalEvent event;
    event.rule_id = static_cast<uint32_t>(RuleId::SelfRssPressure);
    event.event_type = EventType::MonitorMemoryPressure;
    event.level = level;
    event.status = status;
    event.value = rss_mb;
    event.warning_threshold = static_cast<double>(config_.self_rss_soft_limit_mb);
    event.critical_threshold = static_cast<double>(config_.self_rss_hard_limit_mb);
    event.window_sec = config_.fast_collect_interval_ms * config_.continuous_warning_windows / 1000;
    event.continuous_hit_count = self_rss_.hit_count;
    event.hit_count = self_rss_.total_hit_count;
    event.evidence_count = 2;
    set_evidence_key(event.evidence[0], "recovery_threshold_mb");
    event.evidence[0].value = static_cast<double>(config_.self_rss_recovery_mb);
    set_evidence_key(event.evidence[1], "max_observed_rss_mb");
    event.evidence[1].value = self_rss_.max_value;
    return event;
}

InternalEvent RuleEngine::make_queue_event(EventLevel level, EventStatus status, double queue_percent) const
{
    InternalEvent event;
    event.rule_id = static_cast<uint32_t>(RuleId::QueuePressure);
    event.event_type = EventType::QueuePressure;
    event.level = level;
    event.status = status;
    event.value = queue_percent;
    event.warning_threshold = static_cast<double>(config_.queue_warning_percent);
    event.critical_threshold = static_cast<double>(config_.queue_critical_percent);
    event.window_sec = config_.fast_collect_interval_ms * config_.continuous_warning_windows / 1000;
    event.continuous_hit_count = queue_.hit_count;
    event.hit_count = queue_.total_hit_count;
    event.evidence_count = 2;
    set_evidence_key(event.evidence[0], "recovery_threshold_percent");
    event.evidence[0].value = static_cast<double>(config_.queue_recovery_percent);
    set_evidence_key(event.evidence[1], "max_observed_percent");
    event.evidence[1].value = queue_.max_value;
    return event;
}

InternalEvent RuleEngine::make_sched_delay_event(
    const SchedDelaySample& sample,
    EventLevel level,
    EventStatus status,
    double wait_sum_us,
    const RuleContext& context) const
{
    InternalEvent event;
    event.rule_id = static_cast<uint32_t>(RuleId::SchedDelay);
    event.event_type = EventType::SchedDelayRisk;
    event.level = level;
    event.status = status;
    event.pid = sample.pid;
    event.tid = sample.tid;
    event.value = wait_sum_us;
    event.warning_threshold = static_cast<double>(config_.sched_wait_sum_warning_us);
    event.critical_threshold = static_cast<double>(config_.sched_wait_sum_critical_us);
    event.window_sec = config_.fast_collect_interval_ms * config_.sched_continuous_warning_windows / 1000;
    event.continuous_hit_count = context.hit_count;
    event.hit_count = context.total_hit_count;
    event.evidence_count = 3;
    set_evidence_key(event.evidence[0], "delta_involuntary_switches");
    event.evidence[0].value = static_cast<double>(sample.delta_involuntary_switches);
    set_evidence_key(event.evidence[1], "recovery_wait_sum_us");
    event.evidence[1].value = static_cast<double>(config_.sched_wait_sum_recovery_us);
    set_evidence_key(event.evidence[2], "max_observed_wait_sum_us");
    event.evidence[2].value = context.max_value;
    return event;
}

} // namespace lisysm
