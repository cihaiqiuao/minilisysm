#include "minilisysm/core/config.hpp"
#include "minilisysm/core/event.hpp"
#include "minilisysm/storage/jsonl_event_sink.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

#define CHECK(condition)                                                                            \
    do {                                                                                            \
        if (!(condition)) {                                                                         \
            std::cerr << "check failed: " #condition << " at line " << __LINE__ << "\n";          \
            return EXIT_FAILURE;                                                                    \
        }                                                                                           \
    } while (false)

int main()
{
    namespace fs = std::filesystem;

    lisysm::MonitorConfig config;
    config.device_id = "sink-test";
    config.cache_path = "/tmp/minilisysm-jsonl-sink-test";
    config.file_rotate_mb = 1;
    config.cache_max_mb = 4;
    config.critical_fsync = false;
    fs::remove_all(config.cache_path);

    lisysm::JsonlEventSink sink(config);
    lisysm::SpscRingBuffer<lisysm::InternalEvent>* sink_input = sink.add_input_queue(config.event_queue_capacity);
    CHECK(sink_input != nullptr);
    CHECK(sink.start());

    lisysm::InternalEvent event;
    event.sequence = 7;
    event.event_type = lisysm::EventType::IoDelayRisk;
    event.level = lisysm::EventLevel::Warning;
    event.value = 12.0;
    CHECK(sink_input->push(event, event.level));

    sink.stop();

    const fs::path event_path = fs::path(config.cache_path) / "events_000001.jsonl";
    CHECK(fs::exists(event_path));

    std::ifstream input(event_path);
    std::string content((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    CHECK(content.find("\"event_id\":\"sink-test-7\"") != std::string::npos);
    CHECK(content.find("\"event_type\":\"io_delay_risk\"") != std::string::npos);

    const lisysm::SinkStats stats = sink.stats();
    CHECK(stats.accepted_events == 1);
    CHECK(stats.written_events == 1);
    CHECK(stats.write_errors == 0);

    fs::remove_all(config.cache_path);
    return EXIT_SUCCESS;
}
