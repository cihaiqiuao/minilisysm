#include "lisysm/core/config.hpp"
#include "lisysm/core/event.hpp"
#include "lisysm/storage/event_serializer.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

#define CHECK(condition)                                                                            \
    do {                                                                                            \
        if (!(condition)) {                                                                         \
            std::cerr << "check failed: " #condition << " at line " << __LINE__ << "\n";          \
            return EXIT_FAILURE;                                                                    \
        }                                                                                           \
    } while (false)

int main()
{
    lisysm::MonitorConfig config;
    config.device_id = "device-1";
    config.platform = "ccu";
    config.software_version = "0.1";
    config.config_version = "test";

    lisysm::EventSerializer serializer(config);
    lisysm::InternalEvent event;
    event.sequence = 42;
    event.realtime_ms = 1000;
    event.monotonic_ms = 900;
    event.boottime_ms = 901;
    event.event_type = lisysm::EventType::CollectorFailure;
    event.level = lisysm::EventLevel::Warning;
    event.status = lisysm::EventStatus::Active;
    event.value = 3.0;
    event.evidence_count = 1;
    event.evidence[0].key = {'c', 'o', 'l', 'l', 'e', 'c', 't', 'o', 'r', '_', 'i', 'd', '\0'};
    event.evidence[0].value = 1.0;

    std::string line;
    line.reserve(1024);
    const size_t initial_capacity = line.capacity();
    serializer.to_json_line(event, line);
    CHECK(line.find("\"event_type\":\"monitor_collector_failure\"") != std::string::npos);
    CHECK(line.find("\"collector_id\":1.000") != std::string::npos);
    CHECK(line.back() == '\n');
    CHECK(line.capacity() >= initial_capacity);

    event.sequence = 43;
    serializer.to_json_line(event, line);
    CHECK(line.find("\"event_id\":\"device-1-43\"") != std::string::npos);
    CHECK(line.find("\"event_id\":\"device-1-42\"") == std::string::npos);

    const std::string owned = serializer.to_json_line(event);
    CHECK(owned == line);
    return EXIT_SUCCESS;
}
