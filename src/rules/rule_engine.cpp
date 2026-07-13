#include "minilisysm/rules/rule_engine.hpp"
#include "minilisysm/core/time.hpp"

#include <algorithm>
#include <cstring>

namespace lisysm {
namespace {

uint64_t sched_key(int32_t pid, int32_t tid) {
    return (static_cast<uint64_t>(static_cast<uint32_t>(pid)) << 32U) | static_cast<uint32_t>(tid);
}

void set_evidence_key(EvidenceItem& item, const char* key) {
    std::strncpy(item.key.data(), key, kEvidenceKeySize - 1);
    item.key[kEvidenceKeySize - 1] = '\0';
}

void set_event_target(InternalEvent& event, const std::string& target) {
    std::strncpy(event.target.data(), target.c_str(), kEventTargetSize - 1);
    event.target[kEventTargetSize - 1] = '\0';
}

} // namespace

RuleEngine::RuleEngine(const MonitorConfig& config) : config_(config) {
    if (!config_.memory_rule_enable) {
        memory_.state = RuleState::Disabled;
    }
    if (!config_.self_protection_enable) {
        self_rss_.state = RuleState::Disabled;
        queue_.state = RuleState::Disabled;
    }
    if (!config_.cpu_usage_enable) {
        cpu_usage_["total"].state = RuleState::Disabled;
    }
}

std::optional<InternalEvent> RuleEngine::evaluate_memory(const MeminfoSample& sample) {
    if (memory_.state == RuleState::Disabled || !sample.valid) {
        return std::nullopt;
    }

    const double available_mb = static_cast<double>(sample.mem_available_kb) / 1024.0;
    const ThresholdRuleDefinition rule{
        RuleId::MemoryPressure,
        EventType::MemoryPressure,
        ThresholdDirection::LessOrEqual,
        static_cast<double>(config_.mem_available_warning_mb),
        static_cast<double>(config_.mem_available_critical_mb),
        static_cast<double>(config_.mem_available_recovery_mb),
        config_.continuous_warning_windows,
        config_.continuous_critical_windows,
        config_.recovery_windows,
    };
    if (auto level = evaluate_threshold(memory_, rule, available_mb, false, false, false)) {
        return make_memory_event(*level, *level == EventLevel::Recovery ? EventStatus::Resolved : EventStatus::Active,
                                 available_mb);
    }
    return std::nullopt;
}

std::optional<InternalEvent> RuleEngine::evaluate_self_rss(uint64_t rss_kb) {
    if (self_rss_.state == RuleState::Disabled || rss_kb == 0) {
        return std::nullopt;
    }
    const double rss_mb = static_cast<double>(rss_kb) / 1024.0;
    const ThresholdRuleDefinition rule{
        RuleId::SelfRssPressure,
        EventType::MonitorMemoryPressure,
        ThresholdDirection::GreaterOrEqual,
        static_cast<double>(config_.self_rss_soft_limit_mb),
        static_cast<double>(config_.self_rss_hard_limit_mb),
        static_cast<double>(config_.self_rss_recovery_mb),
        config_.continuous_warning_windows,
        config_.continuous_critical_windows,
        config_.self_recovery_windows,
    };
    if (auto level = evaluate_threshold(self_rss_, rule, rss_mb, false, false, false)) {
        return make_self_rss_event(*level, *level == EventLevel::Recovery ? EventStatus::Resolved : EventStatus::Active,
                                   rss_mb);
    }
    return std::nullopt;
}

std::optional<InternalEvent> RuleEngine::evaluate_cpu_usage(const CpuUsageSample& sample) {
    if (!config_.cpu_usage_enable || !sample.valid || sample.cpu.empty()) {
        return std::nullopt;
    }
    RuleContext& context = cpu_usage_[sample.cpu];
    const ThresholdRuleDefinition rule{
        RuleId::CpuUsage,
        EventType::CpuUsageRisk,
        ThresholdDirection::GreaterOrEqual,
        config_.cpu_usage_warning_percent,
        config_.cpu_usage_critical_percent,
        config_.cpu_usage_recovery_percent,
        config_.cpu_usage_continuous_warning_windows,
        config_.cpu_usage_continuous_critical_windows,
        config_.cpu_usage_recovery_windows,
        config_.fast_collect_interval_ms * config_.cpu_usage_continuous_warning_windows / 1000,
    };
    if (auto level = evaluate_threshold(context, rule, sample.usage_percent, false, false, false)) {
        return make_cpu_usage_event(
            *level, *level == EventLevel::Recovery ? EventStatus::Resolved : EventStatus::Active, sample, context);
    }
    return std::nullopt;
}

std::optional<InternalEvent> RuleEngine::evaluate_process_presence(const std::string& name, bool present) {
    if (!config_.process_health_enable || name.empty()) {
        return std::nullopt;
    }
    RuleContext& context = process_health_[name];
    if (!present) {
        ++context.hit_count;
        ++context.total_hit_count;
        context.recovery_count = 0;
        context.max_value = 1.0;
        if (context.state != RuleState::Critical && context.hit_count >= config_.process_missing_critical_windows) {
            context.state = RuleState::Critical;
            return make_process_event(name, EventLevel::Critical, EventStatus::Active, context);
        }
        if (context.state == RuleState::Normal && context.hit_count >= config_.process_missing_warning_windows) {
            context.state = RuleState::Warning;
            return make_process_event(name, EventLevel::Warning, EventStatus::Active, context);
        }
        return std::nullopt;
    }
    context.hit_count = 0;
    if (context.state != RuleState::Normal) {
        ++context.recovery_count;
        if (context.recovery_count >= config_.process_recovery_windows) {
            context.state = RuleState::Normal;
            return make_process_event(name, EventLevel::Recovery, EventStatus::Resolved, context);
        }
    } else {
        context.recovery_count = 0;
    }
    return std::nullopt;
}

std::optional<InternalEvent> RuleEngine::evaluate_queue(const QueueSnapshot& snapshot) {
    if (queue_.state == RuleState::Disabled || (snapshot.capacity == 0 && snapshot.sink_capacity == 0)) {
        return std::nullopt;
    }
    const double source_percent =
        snapshot.capacity == 0 ? 0.0
                               : static_cast<double>(snapshot.depth) * 100.0 / static_cast<double>(snapshot.capacity);
    const double sink_percent = snapshot.sink_capacity == 0 ? 0.0
                                                            : static_cast<double>(snapshot.sink_depth) * 100.0 /
                                                                  static_cast<double>(snapshot.sink_capacity);
    const double queue_percent = std::max(source_percent, sink_percent);
    const uint64_t total_drop_count =
        snapshot.dropped_count + snapshot.sink_dropped_count + snapshot.dispatcher_sink_push_failures;
    const uint64_t total_critical_drop_count = snapshot.dropped_critical_count + snapshot.sink_dropped_critical_count;
    const bool new_drop = total_drop_count > last_queue_drop_count_;
    const bool new_critical_drop = total_critical_drop_count > last_queue_critical_drop_count_;
    const bool new_dispatcher_failure = snapshot.dispatcher_sink_push_failures > last_queue_dispatcher_failure_count_;
    last_queue_drop_count_ = total_drop_count;
    last_queue_critical_drop_count_ = total_critical_drop_count;
    last_queue_dispatcher_failure_count_ = snapshot.dispatcher_sink_push_failures;
    const ThresholdRuleDefinition rule{
        RuleId::QueuePressure,
        EventType::QueuePressure,
        ThresholdDirection::GreaterOrEqual,
        static_cast<double>(config_.queue_warning_percent),
        static_cast<double>(config_.queue_critical_percent),
        static_cast<double>(config_.queue_recovery_percent),
        config_.continuous_warning_windows,
        config_.continuous_critical_windows,
        config_.self_recovery_windows,
        config_.fast_collect_interval_ms * config_.continuous_warning_windows / 1000,
    };
    if (auto level = evaluate_threshold(queue_, rule, queue_percent, new_drop || new_dispatcher_failure,
                                        new_critical_drop, new_drop || new_dispatcher_failure)) {
        return make_queue_event(*level, *level == EventLevel::Recovery ? EventStatus::Resolved : EventStatus::Active,
                                queue_percent, snapshot);
    }
    return std::nullopt;
}

std::optional<InternalEvent> RuleEngine::evaluate_sched_delay(const SchedDelaySample& sample) {
    if (!config_.sched_delay_enable || !sample.valid) {
        return std::nullopt;
    }
    RuleContext& context = sched_delay_[sched_key(sample.pid, sample.tid)];
    const double wait_us = static_cast<double>(sample.delta_wait_sum_us);
    const bool switch_hit = sample.delta_involuntary_switches >= config_.sched_involuntary_switch_warning;
    const ThresholdRuleDefinition rule{
        RuleId::SchedDelay,
        EventType::SchedDelayRisk,
        ThresholdDirection::GreaterOrEqual,
        static_cast<double>(config_.sched_wait_sum_warning_us),
        static_cast<double>(config_.sched_wait_sum_critical_us),
        static_cast<double>(config_.sched_wait_sum_recovery_us),
        config_.sched_continuous_warning_windows,
        config_.sched_continuous_critical_windows,
        config_.sched_recovery_windows,
    };
    const double evaluated_wait_us = switch_hit ? wait_us : 0.0;
    if (auto level = evaluate_threshold(context, rule, evaluated_wait_us, false, false, false)) {
        return make_sched_delay_event(sample, *level,
                                      *level == EventLevel::Recovery ? EventStatus::Resolved : EventStatus::Active,
                                      wait_us, context);
    }
    return std::nullopt;
}

std::optional<InternalEvent> RuleEngine::evaluate_io_delay(const IoDelaySample& sample) {
    if (!config_.io_delay_enable || !sample.valid || sample.device.empty()) {
        return std::nullopt;
    }
    RuleContext& context = io_delay_[sample.device];
    const double await_ms = sample.avg_await_ms;
    const bool critical_util_hit = sample.util_percent >= config_.io_util_critical_percent && sample.in_flight > 0;
    const bool warning_util_hit = sample.util_percent >= config_.io_util_warning_percent && sample.in_flight > 0;
    const bool recovery_blocked = sample.util_percent > config_.io_util_recovery_percent;
    const ThresholdRuleDefinition rule{
        RuleId::IoDelay,
        EventType::IoDelayRisk,
        ThresholdDirection::GreaterOrEqual,
        config_.io_await_warning_ms,
        config_.io_await_critical_ms,
        config_.io_await_recovery_ms,
        config_.io_continuous_warning_windows,
        config_.io_continuous_critical_windows,
        config_.io_recovery_windows,
        config_.fast_collect_interval_ms * config_.io_continuous_warning_windows / 1000,
    };
    if (auto level =
            evaluate_threshold(context, rule, await_ms, warning_util_hit, critical_util_hit, recovery_blocked)) {
        return make_io_delay_event(sample, *level,
                                   *level == EventLevel::Recovery ? EventStatus::Resolved : EventStatus::Active,
                                   await_ms, context);
    }
    return std::nullopt;
}

std::optional<EventLevel> RuleEngine::evaluate_threshold(RuleContext& context,
                                                         const ThresholdRuleDefinition& definition, double value,
                                                         bool external_warning_trigger, bool external_critical_trigger,
                                                         bool external_recovery_blocked) {
    context.max_value = std::max(context.max_value, value);
    const auto above_or_equal = [](double lhs, double rhs) { return lhs >= rhs; };
    const auto below_or_equal = [](double lhs, double rhs) { return lhs <= rhs; };
    const bool threshold_critical_hit = definition.direction == ThresholdDirection::GreaterOrEqual
                                            ? above_or_equal(value, definition.critical_threshold)
                                            : below_or_equal(value, definition.critical_threshold);
    const bool critical_hit = threshold_critical_hit || external_critical_trigger;
    if (critical_hit) {
        ++context.hit_count;
        ++context.critical_hit_count;
        ++context.total_hit_count;
        context.recovery_count = 0;
        if ((external_critical_trigger || context.critical_hit_count >= definition.critical_windows) &&
            context.state != RuleState::Critical) {
            context.state = RuleState::Critical;
            return EventLevel::Critical;
        }
        return std::nullopt;
    }

    const bool threshold_warning_hit = definition.direction == ThresholdDirection::GreaterOrEqual
                                           ? above_or_equal(value, definition.warning_threshold)
                                           : below_or_equal(value, definition.warning_threshold);
    const bool warning_hit = threshold_warning_hit || external_warning_trigger;
    if (warning_hit) {
        ++context.hit_count;
        ++context.total_hit_count;
        context.critical_hit_count = 0;
        context.recovery_count = 0;
        if ((external_warning_trigger || context.hit_count >= definition.warning_windows) &&
            context.state == RuleState::Normal) {
            context.state = RuleState::Warning;
            return EventLevel::Warning;
        }
        return std::nullopt;
    }

    if (recovered(definition, value, !external_recovery_blocked) && context.state != RuleState::Normal) {
        ++context.recovery_count;
        if (context.recovery_count >= definition.recovery_windows) {
            context.state = RuleState::Normal;
            context.hit_count = 0;
            context.critical_hit_count = 0;
            return EventLevel::Recovery;
        }
        return std::nullopt;
    }

    context.hit_count = 0;
    context.critical_hit_count = 0;
    context.recovery_count = 0;
    return std::nullopt;
}

bool RuleEngine::recovered(const ThresholdRuleDefinition& definition, double value,
                           bool extra_recovery_condition) const {
    if (!extra_recovery_condition) {
        return false;
    }
    return definition.direction == ThresholdDirection::GreaterOrEqual ? value <= definition.recovery_threshold
                                                                      : value >= definition.recovery_threshold;
}

InternalEvent RuleEngine::make_memory_event(EventLevel level, EventStatus status, double value_mb) const {
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

InternalEvent RuleEngine::make_self_rss_event(EventLevel level, EventStatus status, double rss_mb) const {
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

InternalEvent RuleEngine::make_cpu_usage_event(EventLevel level, EventStatus status, const CpuUsageSample& sample,
                                               const RuleContext& context) const {
    InternalEvent event;
    event.rule_id = static_cast<uint32_t>(RuleId::CpuUsage);
    event.event_type = EventType::CpuUsageRisk;
    event.level = level;
    event.status = status;
    set_event_target(event, sample.cpu);
    event.value = sample.usage_percent;
    event.warning_threshold = config_.cpu_usage_warning_percent;
    event.critical_threshold = config_.cpu_usage_critical_percent;
    event.window_sec = config_.fast_collect_interval_ms * config_.cpu_usage_continuous_warning_windows / 1000;
    event.continuous_hit_count = context.hit_count;
    event.hit_count = context.total_hit_count;
    event.evidence_count = 4;
    set_evidence_key(event.evidence[0], "recovery_percent");
    event.evidence[0].value = config_.cpu_usage_recovery_percent;
    set_evidence_key(event.evidence[1], "max_observed_percent");
    event.evidence[1].value = context.max_value;
    set_evidence_key(event.evidence[2], "delta_total_jiffies");
    event.evidence[2].value = static_cast<double>(sample.delta_total_jiffies);
    set_evidence_key(event.evidence[3], "delta_idle_jiffies");
    event.evidence[3].value = static_cast<double>(sample.delta_idle_jiffies);
    return event;
}

InternalEvent RuleEngine::make_process_event(const std::string& name, EventLevel level, EventStatus status,
                                             const RuleContext& context) const {
    InternalEvent event;
    event.rule_id = static_cast<uint32_t>(RuleId::WhitelistedProcess);
    event.event_type = EventType::WhitelistedProcessRisk;
    event.level = level;
    event.status = status;
    set_event_target(event, name);
    event.value = status == EventStatus::Resolved ? 0.0 : 1.0;
    event.warning_threshold = 1.0;
    event.critical_threshold = 1.0;
    event.window_sec = config_.fast_collect_interval_ms * config_.process_missing_warning_windows / 1000;
    event.continuous_hit_count = context.hit_count;
    event.hit_count = context.total_hit_count;
    event.evidence_count = 2;
    set_evidence_key(event.evidence[0], "missing_warning_windows");
    event.evidence[0].value = static_cast<double>(config_.process_missing_warning_windows);
    set_evidence_key(event.evidence[1], "missing_critical_windows");
    event.evidence[1].value = static_cast<double>(config_.process_missing_critical_windows);
    return event;
}

InternalEvent RuleEngine::make_queue_event(EventLevel level, EventStatus status, double queue_percent,
                                           const QueueSnapshot& snapshot) const {
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
    event.evidence_count = 6;
    const double source_percent =
        snapshot.capacity == 0 ? 0.0
                               : static_cast<double>(snapshot.depth) * 100.0 / static_cast<double>(snapshot.capacity);
    const double sink_percent = snapshot.sink_capacity == 0 ? 0.0
                                                            : static_cast<double>(snapshot.sink_depth) * 100.0 /
                                                                  static_cast<double>(snapshot.sink_capacity);
    const double source_high_percent = snapshot.capacity == 0 ? 0.0
                                                              : static_cast<double>(snapshot.high_watermark) * 100.0 /
                                                                    static_cast<double>(snapshot.capacity);
    const double sink_high_percent =
        snapshot.sink_capacity == 0
            ? 0.0
            : static_cast<double>(snapshot.sink_high_watermark) * 100.0 / static_cast<double>(snapshot.sink_capacity);
    set_evidence_key(event.evidence[0], "source_queue_percent");
    event.evidence[0].value = source_percent;
    set_evidence_key(event.evidence[1], "sink_queue_percent");
    event.evidence[1].value = sink_percent;
    set_evidence_key(event.evidence[2], "total_dropped_count");
    event.evidence[2].value = static_cast<double>(snapshot.dropped_count + snapshot.sink_dropped_count);
    set_evidence_key(event.evidence[3], "dispatcher_failures");
    event.evidence[3].value = static_cast<double>(snapshot.dispatcher_sink_push_failures);
    set_evidence_key(event.evidence[4], "critical_dropped_count");
    event.evidence[4].value =
        static_cast<double>(snapshot.dropped_critical_count + snapshot.sink_dropped_critical_count);
    set_evidence_key(event.evidence[5], "high_watermark_percent");
    event.evidence[5].value = std::max(source_high_percent, sink_high_percent);
    return event;
}

InternalEvent RuleEngine::make_sched_delay_event(const SchedDelaySample& sample, EventLevel level, EventStatus status,
                                                 double wait_sum_us, const RuleContext& context) const {
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
    event.evidence_count = 6;
    set_evidence_key(event.evidence[0], "delta_involuntary_switches");
    event.evidence[0].value = static_cast<double>(sample.delta_involuntary_switches);
    set_evidence_key(event.evidence[1], "recovery_wait_sum_us");
    event.evidence[1].value = static_cast<double>(config_.sched_wait_sum_recovery_us);
    set_evidence_key(event.evidence[2], "max_observed_wait_sum_us");
    event.evidence[2].value = context.max_value;
    set_evidence_key(event.evidence[3], "max_wait_us");
    event.evidence[3].value = static_cast<double>(sample.max_wait_us);
    set_evidence_key(event.evidence[4], "avg_wait_us");
    event.evidence[4].value = static_cast<double>(sample.avg_wait_us);
    set_evidence_key(event.evidence[5], "aggregate_count");
    event.evidence[5].value = static_cast<double>(sample.aggregate_count);
    return event;
}

InternalEvent RuleEngine::make_io_delay_event(const IoDelaySample& sample, EventLevel level, EventStatus status,
                                              double await_ms, const RuleContext& context) const {
    InternalEvent event;
    event.rule_id = static_cast<uint32_t>(RuleId::IoDelay);
    event.event_type = EventType::IoDelayRisk;
    event.level = level;
    event.status = status;
    set_event_target(event, sample.device);
    event.value = await_ms;
    event.warning_threshold = config_.io_await_warning_ms;
    event.critical_threshold = config_.io_await_critical_ms;
    event.window_sec = config_.fast_collect_interval_ms * config_.io_continuous_warning_windows / 1000;
    event.continuous_hit_count = context.hit_count;
    event.hit_count = context.total_hit_count;
    event.evidence_count = 5;
    set_evidence_key(event.evidence[0], "delta_io_count");
    event.evidence[0].value = static_cast<double>(sample.delta_io_count);
    set_evidence_key(event.evidence[1], "util_percent");
    event.evidence[1].value = sample.util_percent;
    set_evidence_key(event.evidence[2], "in_flight");
    event.evidence[2].value = static_cast<double>(sample.in_flight);
    set_evidence_key(event.evidence[3], "recovery_await_ms");
    event.evidence[3].value = config_.io_await_recovery_ms;
    set_evidence_key(event.evidence[4], "max_observed_await_ms");
    event.evidence[4].value = context.max_value;
    return event;
}

} // namespace lisysm
