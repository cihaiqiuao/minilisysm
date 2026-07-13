#pragma once

#include <array>
#include <cstdint>

namespace lisysm {

constexpr uint32_t kMaxEvidence = 6;
constexpr uint32_t kEvidenceKeySize = 32;
constexpr uint32_t kEventTargetSize = 32;

enum class EventLevel : uint8_t {
    Info = 0,
    Recovery = 1,
    Warning = 2,
    Critical = 3,
};

enum class EventStatus : uint8_t {
    Active = 0,
    Recovering = 1,
    Resolved = 2,
};

enum class EventType : uint32_t {
    MonitorStarted = 1,
    MemoryPressure = 2,
    MonitorOverrun = 3,
    QueuePressure = 4,
    StoragePressure = 5,
    CollectorFailure = 6,
    MonitorMemoryPressure = 7,
    SchedDelayRisk = 8,
    IoDelayRisk = 9,
    CpuUsageRisk = 10,
    WhitelistedProcessMemoryRisk = 11,
};

struct EvidenceItem {
    std::array<char, kEvidenceKeySize> key{};
    double value{0.0};
};

struct InternalEvent {
    uint64_t sequence{0};
    uint64_t realtime_ms{0};
    uint64_t monotonic_ms{0};
    uint64_t boottime_ms{0};
    uint32_t rule_id{0};
    EventType event_type{EventType::MonitorStarted};
    EventLevel level{EventLevel::Info};
    EventStatus status{EventStatus::Active};
    std::array<char, kEventTargetSize> target{};
    int32_t pid{-1};
    int32_t tid{-1};
    double value{0.0};
    double warning_threshold{0.0};
    double critical_threshold{0.0};
    uint32_t window_sec{0};
    uint32_t continuous_hit_count{0};
    uint64_t first_seen_ms{0};
    uint64_t last_seen_ms{0};
    uint64_t hit_count{0};
    uint32_t evidence_count{0};
    std::array<EvidenceItem, kMaxEvidence> evidence{};
};

const char* to_string(EventLevel level);
const char* to_string(EventStatus status);
const char* to_string(EventType type);

} // namespace lisysm
