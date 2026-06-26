#pragma once

#include "minilisysm/core/config.hpp"
#include "minilisysm/core/event.hpp"
#include "minilisysm/collectors/io_delay_collector.hpp"
#include "minilisysm/collectors/meminfo_collector.hpp"
#include "minilisysm/interfaces/sched_delay_collector.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <unordered_map>

namespace lisysm {

enum class RuleState : uint8_t {
    Normal = 0,
    Warning = 1,
    Critical = 2,
    Recovering = 3,
    Disabled = 4,
};

enum class RuleId : uint32_t {
    MemoryPressure = 1001,
    SelfRssPressure = 2001,
    QueuePressure = 2002,
    SchedDelay = 3001,
    IoDelay = 4001,
};

enum class ThresholdDirection : uint8_t {
    LessOrEqual,
    GreaterOrEqual,
};

struct ThresholdRuleDefinition {
    RuleId rule_id{RuleId::MemoryPressure};
    EventType event_type{EventType::MemoryPressure};
    ThresholdDirection direction{ThresholdDirection::GreaterOrEqual};
    double warning_threshold{0.0};
    double critical_threshold{0.0};
    double recovery_threshold{0.0};
    uint32_t warning_windows{1};
    uint32_t critical_windows{1};
    uint32_t recovery_windows{1};
    uint32_t window_sec{0};
};

struct QueueSnapshot {
    uint64_t push_fail_count{0};
    uint64_t dropped_count{0};
    uint64_t dropped_critical_count{0};
    uint64_t reserve_reject_count{0};
    uint64_t dispatcher_sink_push_failures{0};
    uint64_t sink_dropped_count{0};
    uint64_t sink_dropped_critical_count{0};
    uint64_t sink_reserve_reject_count{0};
    size_t depth{0};
    size_t capacity{0};
    size_t high_watermark{0};
    size_t sink_depth{0};
    size_t sink_capacity{0};
    size_t sink_high_watermark{0};
};

struct RuleContext {
    RuleState state{RuleState::Normal};
    uint32_t hit_count{0};
    uint32_t critical_hit_count{0};
    uint32_t recovery_count{0};
    uint64_t total_hit_count{0};
    uint64_t first_seen_ms{0};
    uint64_t last_seen_ms{0};
    double max_value{0.0};
};

class RuleEngine {
public:
    explicit RuleEngine(const MonitorConfig& config);
    std::optional<InternalEvent> evaluate_memory(const MeminfoSample& sample);
    std::optional<InternalEvent> evaluate_self_rss(uint64_t rss_kb);
    std::optional<InternalEvent> evaluate_queue(const QueueSnapshot& snapshot);
    std::optional<InternalEvent> evaluate_sched_delay(const SchedDelaySample& sample);
    std::optional<InternalEvent> evaluate_io_delay(const IoDelaySample& sample);

private:
    InternalEvent make_memory_event(EventLevel level, EventStatus status, double value_mb) const;
    InternalEvent make_self_rss_event(EventLevel level, EventStatus status, double rss_mb) const;
    InternalEvent make_queue_event(
        EventLevel level,
        EventStatus status,
        double queue_percent,
        const QueueSnapshot& snapshot) const;
    InternalEvent make_sched_delay_event(
        const SchedDelaySample& sample,
        EventLevel level,
        EventStatus status,
        double wait_sum_us,
        const RuleContext& context) const;
    InternalEvent make_io_delay_event(
        const IoDelaySample& sample,
        EventLevel level,
        EventStatus status,
        double await_ms,
        const RuleContext& context) const;
    std::optional<EventLevel> evaluate_threshold(
        RuleContext& context,
        const ThresholdRuleDefinition& definition,
        double value,
        bool external_warning_trigger,
        bool external_critical_trigger,
        bool external_recovery_blocked);
    bool recovered(
        const ThresholdRuleDefinition& definition,
        double value,
        bool extra_recovery_condition) const;

    const MonitorConfig& config_;
    RuleContext memory_;
    RuleContext self_rss_;
    RuleContext queue_;
    uint64_t last_queue_drop_count_{0};
    uint64_t last_queue_critical_drop_count_{0};
    uint64_t last_queue_dispatcher_failure_count_{0};
    std::unordered_map<uint64_t, RuleContext> sched_delay_;
    std::unordered_map<std::string, RuleContext> io_delay_;
};

} // namespace lisysm
