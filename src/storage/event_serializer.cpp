#include "minilisysm/storage/event_serializer.hpp"

#include <charconv>
#include <cstdio>
#include <string_view>

namespace lisysm {
namespace {

void append_json_string(std::string& output, std::string_view value)
{
    output.push_back('"');
    for (const char c : value) {
        switch (c) {
        case '"': output += "\\\""; break;
        case '\\': output += "\\\\"; break;
        case '\b': output += "\\b"; break;
        case '\f': output += "\\f"; break;
        case '\n': output += "\\n"; break;
        case '\r': output += "\\r"; break;
        case '\t': output += "\\t"; break;
        default: output.push_back(c); break;
        }
    }
    output.push_back('"');
}

template <typename T>
void append_integer(std::string& output, T value)
{
    char buffer[32];
    auto [ptr, ec] = std::to_chars(buffer, buffer + sizeof(buffer), value);
    if (ec == std::errc{}) {
        output.append(buffer, static_cast<size_t>(ptr - buffer));
    }
}

void append_double(std::string& output, double value)
{
    char buffer[64];
    const int written = std::snprintf(buffer, sizeof(buffer), "%.3f", value);
    if (written > 0) {
        output.append(buffer, static_cast<size_t>(written));
    }
}

void append_field_name(std::string& output, const char* name)
{
    output += ",\"";
    output += name;
    output += "\":";
}

} // namespace

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
    case EventType::MonitorMemoryPressure: return "monitor_memory_pressure";
    case EventType::SchedDelayRisk: return "sched_delay_risk";
    case EventType::IoDelayRisk: return "io_delay_risk";
    }
    return "unknown";
}

EventSerializer::EventSerializer(const MonitorConfig& config) : config_(config) {}

std::string EventSerializer::to_json_line(const InternalEvent& event) const
{
    std::string output;
    to_json_line(event, output);
    return output;
}

void EventSerializer::to_json_line(const InternalEvent& event, std::string& output) const
{
    output.clear();
    output.reserve(1024);
    output += "{\"event_id\":";
    output.push_back('"');
    for (const char c : config_.device_id) {
        switch (c) {
        case '"': output += "\\\""; break;
        case '\\': output += "\\\\"; break;
        case '\b': output += "\\b"; break;
        case '\f': output += "\\f"; break;
        case '\n': output += "\\n"; break;
        case '\r': output += "\\r"; break;
        case '\t': output += "\\t"; break;
        default: output.push_back(c); break;
        }
    }
    output.push_back('-');
    append_integer(output, event.sequence);
    output.push_back('"');
    append_field_name(output, "device_id");
    append_json_string(output, config_.device_id);
    append_field_name(output, "platform");
    append_json_string(output, config_.platform);
    append_field_name(output, "software_version");
    append_json_string(output, config_.software_version);
    append_field_name(output, "config_version");
    append_json_string(output, config_.config_version);
    append_field_name(output, "timestamp_ms");
    append_integer(output, event.realtime_ms);
    append_field_name(output, "monotonic_ms");
    append_integer(output, event.monotonic_ms);
    append_field_name(output, "boottime_ms");
    append_integer(output, event.boottime_ms);
    append_field_name(output, "module");
    append_json_string(output, "linux_stability_monitor");
    append_field_name(output, "rule_id");
    append_integer(output, event.rule_id);
    append_field_name(output, "event_type");
    append_json_string(output, to_string(event.event_type));
    append_field_name(output, "level");
    append_json_string(output, to_string(event.level));
    append_field_name(output, "status");
    append_json_string(output, to_string(event.status));
    append_field_name(output, "target");
    append_json_string(output, event.target.data());
    append_field_name(output, "pid");
    append_integer(output, event.pid);
    append_field_name(output, "tid");
    append_integer(output, event.tid);
    append_field_name(output, "value");
    append_double(output, event.value);
    append_field_name(output, "warning_threshold");
    append_double(output, event.warning_threshold);
    append_field_name(output, "critical_threshold");
    append_double(output, event.critical_threshold);
    append_field_name(output, "window_sec");
    append_integer(output, event.window_sec);
    append_field_name(output, "continuous_hit_count");
    append_integer(output, event.continuous_hit_count);
    append_field_name(output, "first_seen_ms");
    append_integer(output, event.first_seen_ms);
    append_field_name(output, "last_seen_ms");
    append_integer(output, event.last_seen_ms);
    append_field_name(output, "hit_count");
    append_integer(output, event.hit_count);
    output += ",\"evidence\":{";
    for (uint32_t i = 0; i < event.evidence_count && i < event.evidence.size(); ++i) {
        if (i != 0) {
            output.push_back(',');
        }
        append_json_string(output, event.evidence[i].key.data());
        output.push_back(':');
        append_double(output, event.evidence[i].value);
    }
    output += "}}\n";
}

} // namespace lisysm
