#include "lisysm/event_serializer.hpp"

#include <iomanip>
#include <sstream>

namespace lisysm {

const char* to_string(EventLevel level)
{
    switch (level) {
    case EventLevel::Critical: return "critical";
    case EventLevel::Warning: return "warning";
    case EventLevel::Recovery: return "recovery";
    case EventLevel::Info: return "info";
    }
    return "unknown";
}

const char* to_string(EventStatus status)
{
    switch (status) {
    case EventStatus::Active: return "active";
    case EventStatus::Recovering: return "recovering";
    case EventStatus::Resolved: return "resolved";
    }
    return "unknown";
}

const char* to_string(EventType type)
{
    switch (type) {
    case EventType::MonitorStarted: return "monitor_started";
    case EventType::MemoryPressure: return "memory_pressure";
    case EventType::MonitorOverrun: return "monitor_overrun";
    case EventType::QueuePressure: return "monitor_queue_pressure";
    case EventType::StoragePressure: return "monitor_storage_pressure";
    case EventType::CollectorFailure: return "monitor_collector_failure";
    }
    return "unknown";
}

EventSerializer::EventSerializer(const MonitorConfig& config) : config_(config) {}

std::string EventSerializer::to_json_line(const InternalEvent& event) const
{
    std::ostringstream out;
    out << std::fixed << std::setprecision(3);
    out << "{\"event_id\":\"" << config_.device_id << "-" << event.sequence << "\"";
    out << ",\"device_id\":\"" << config_.device_id << "\"";
    out << ",\"platform\":\"" << config_.platform << "\"";
    out << ",\"software_version\":\"" << config_.software_version << "\"";
    out << ",\"config_version\":\"" << config_.config_version << "\"";
    out << ",\"timestamp_ms\":" << event.realtime_ms;
    out << ",\"monotonic_ms\":" << event.monotonic_ms;
    out << ",\"boottime_ms\":" << event.boottime_ms;
    out << ",\"module\":\"linux_stability_monitor\"";
    out << ",\"rule_id\":" << event.rule_id;
    out << ",\"event_type\":\"" << to_string(event.event_type) << "\"";
    out << ",\"level\":\"" << to_string(event.level) << "\"";
    out << ",\"status\":\"" << to_string(event.status) << "\"";
    out << ",\"pid\":" << event.pid;
    out << ",\"tid\":" << event.tid;
    out << ",\"value\":" << event.value;
    out << ",\"warning_threshold\":" << event.warning_threshold;
    out << ",\"critical_threshold\":" << event.critical_threshold;
    out << ",\"window_sec\":" << event.window_sec;
    out << ",\"continuous_hit_count\":" << event.continuous_hit_count;
    out << ",\"first_seen_ms\":" << event.first_seen_ms;
    out << ",\"last_seen_ms\":" << event.last_seen_ms;
    out << ",\"hit_count\":" << event.hit_count;
    out << ",\"evidence\":{";
    for (uint32_t i = 0; i < event.evidence_count && i < event.evidence.size(); ++i) {
        if (i != 0) {
            out << ',';
        }
        out << "\"" << event.evidence[i].key.data() << "\":" << event.evidence[i].value;
    }
    out << "}}\n";
    return out.str();
}

} // namespace lisysm
