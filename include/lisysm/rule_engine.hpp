#pragma once

#include "lisysm/config.hpp"
#include "lisysm/event.hpp"
#include "lisysm/meminfo_collector.hpp"

#include <cstdint>
#include <optional>

namespace lisysm {

enum class RuleState : uint8_t {
    Normal = 0,
    Warning = 1,
    Critical = 2,
    Recovering = 3,
    Disabled = 4,
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

private:
    InternalEvent make_memory_event(EventLevel level, EventStatus status, double value_mb) const;

    const MonitorConfig& config_;
    RuleContext memory_;
};

} // namespace lisysm
