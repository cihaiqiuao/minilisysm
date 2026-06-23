#include "lisysm/rule_engine.hpp"
#include "lisysm/time.hpp"

#include <algorithm>
#include <cstring>

namespace lisysm {

RuleEngine::RuleEngine(const MonitorConfig& config) : config_(config)
{
    if (!config_.memory_rule_enable) {
        memory_.state = RuleState::Disabled;
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

InternalEvent RuleEngine::make_memory_event(EventLevel level, EventStatus status, double value_mb) const
{
    InternalEvent event;
    event.rule_id = 1001;
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
    std::strncpy(event.evidence[0].key.data(), "recovery_threshold_mb", kEvidenceKeySize - 1);
    event.evidence[0].value = static_cast<double>(config_.mem_available_recovery_mb);
    std::strncpy(event.evidence[1].key.data(), "max_observed_available_mb", kEvidenceKeySize - 1);
    event.evidence[1].value = memory_.max_value;
    return event;
}

} // namespace lisysm
