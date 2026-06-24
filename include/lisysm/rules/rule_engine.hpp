#pragma once

#include "lisysm/core/config.hpp"
#include "lisysm/core/event.hpp"
#include "lisysm/collectors/meminfo_collector.hpp"
#include "lisysm/collectors/sched_delay_collector.hpp"

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
};

struct QueueSnapshot {
    uint64_t push_fail_count{0};
    uint64_t dropped_count{0};
    size_t depth{0};
    size_t capacity{0};
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

private:
    InternalEvent make_memory_event(EventLevel level, EventStatus status, double value_mb) const;
    InternalEvent make_self_rss_event(EventLevel level, EventStatus status, double rss_mb) const;
    InternalEvent make_queue_event(EventLevel level, EventStatus status, double queue_percent) const;
    InternalEvent make_sched_delay_event(
        const SchedDelaySample& sample,
        EventLevel level,
        EventStatus status,
        double wait_sum_us,
        const RuleContext& context) const;

    const MonitorConfig& config_;
    RuleContext memory_;
    RuleContext self_rss_;
    RuleContext queue_;
    std::unordered_map<uint64_t, RuleContext> sched_delay_;
};

} // namespace lisysm
