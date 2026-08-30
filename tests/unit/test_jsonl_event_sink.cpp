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

#if defined(__linux__)
#include <sys/stat.h>
#endif

#define CHECK(condition)                                                                                               \
    do {                                                                                                               \
        if (!(condition)) {                                                                                            \
            std::cerr << "check failed: " #condition << " at line " << __LINE__ << "\n";                               \
            return EXIT_FAILURE;                                                                                       \
        }                                                                                                              \
    } while (false)

int main() {
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
    event.level = lisysm::EventLevel::Critical;
    event.value = 12.0;
    CHECK(sink_input->push(event, event.level));

    sink.stop();

    fs::path event_path;
    for (const auto& entry : fs::directory_iterator(fs::path(config.cache_path) / "jsonl")) {
        if (entry.path().extension() == ".jsonl") {
            event_path = entry.path();
        }
    }
    fs::path summary_path;
    for (const auto& entry : fs::directory_iterator(fs::path(config.cache_path) / "summary")) {
        if (entry.path().extension() == ".log") {
            summary_path = entry.path();
        }
    }
    CHECK(!event_path.empty());
    CHECK(!summary_path.empty());
    CHECK(event_path.filename().string().find("minilisysm-events-") == 0);
    CHECK(event_path.filename().string().find("-part000001.jsonl") != std::string::npos);
    CHECK(summary_path.filename().string().find("minilisysm-events-") == 0);
    CHECK(summary_path.filename().string().find("-part000001.summary.log") != std::string::npos);

    std::ifstream input(event_path);
    std::string content((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    CHECK(content.find("\"event_id\":\"sink-test-7\"") != std::string::npos);
    CHECK(content.find("\"event_type\":\"io_delay_risk\"") != std::string::npos);

    std::ifstream summary_input(summary_path);
    std::string summary((std::istreambuf_iterator<char>(summary_input)), std::istreambuf_iterator<char>());
    CHECK(summary.find("[error] I/O 延迟风险 (io_delay_risk)") != std::string::npos);
    CHECK(summary.find("当前值(value)=12.000") != std::string::npos);
    CHECK(summary.find("事件ID=sink-test-7") != std::string::npos);
    CHECK(summary.find("\033[") == std::string::npos);

    const lisysm::SinkStats stats = sink.stats();
    CHECK(stats.accepted_events == 1);
    CHECK(stats.written_events == 1);
    CHECK(stats.write_errors == 0);

    fs::remove_all(config.cache_path);

#if defined(__linux__)
    {
        lisysm::MonitorConfig sync_config;
        sync_config.device_id = "sink-sync-test";
        sync_config.cache_path = "/tmp/minilisysm-jsonl-sink-sync-test";
        sync_config.summary_enable = false;
        sync_config.critical_fsync = true;
        sync_config.max_fsync_per_minute = 1;
        sync_config.sink_idle_sleep_ms = 1;
        fs::remove_all(sync_config.cache_path);

        lisysm::JsonlEventSink sync_sink(sync_config);
        auto* sync_input = sync_sink.add_input_queue(sync_config.event_queue_capacity);
        CHECK(sync_input != nullptr);
        CHECK(sync_sink.start());

        lisysm::InternalEvent critical_event;
        critical_event.event_type = lisysm::EventType::IoDelayRisk;
        critical_event.level = lisysm::EventLevel::Critical;
        critical_event.sequence = 1;
        CHECK(sync_input->push(critical_event, critical_event.level));
        critical_event.sequence = 2;
        CHECK(sync_input->push(critical_event, critical_event.level));
        sync_sink.stop();

        const lisysm::SinkStats sync_stats = sync_sink.stats();
        CHECK(sync_stats.fsync_count == 1);
        CHECK(sync_stats.fsync_failures == 0);
        CHECK(sync_stats.fsync_rate_limited == 1);
        fs::remove_all(sync_config.cache_path);
    }

    {
        lisysm::MonitorConfig limited_config;
        limited_config.device_id = "sink-sync-disabled-by-limit-test";
        limited_config.cache_path = "/tmp/minilisysm-jsonl-sink-zero-sync-limit-test";
        limited_config.summary_enable = false;
        limited_config.critical_fsync = true;
        limited_config.max_fsync_per_minute = 0;
        limited_config.sink_idle_sleep_ms = 1;
        fs::remove_all(limited_config.cache_path);

        lisysm::JsonlEventSink limited_sink(limited_config);
        auto* limited_input = limited_sink.add_input_queue(limited_config.event_queue_capacity);
        CHECK(limited_input != nullptr);
        CHECK(limited_sink.start());

        lisysm::InternalEvent critical_event;
        critical_event.event_type = lisysm::EventType::IoDelayRisk;
        critical_event.level = lisysm::EventLevel::Critical;
        critical_event.sequence = 3;
        CHECK(limited_input->push(critical_event, critical_event.level));

        for (int attempt = 0; attempt < 200; ++attempt) {
            if (limited_sink.stats().fsync_rate_limited == 1) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        const lisysm::SinkStats limited_stats = limited_sink.stats();
        CHECK(limited_stats.written_events == 1);
        CHECK(limited_stats.fsync_count == 0);
        CHECK(limited_stats.fsync_failures == 0);
        CHECK(limited_stats.fsync_rate_limited == 1);

        fs::path limited_path;
        for (const auto& entry : fs::directory_iterator(fs::path(limited_config.cache_path) / "jsonl")) {
            if (entry.is_regular_file() && entry.path().extension() == ".jsonl") {
                limited_path = entry.path();
                break;
            }
        }
        CHECK(!limited_path.empty());
        std::ifstream limited_input_stream(limited_path, std::ios::binary);
        const std::string limited_content((std::istreambuf_iterator<char>(limited_input_stream)),
                                          std::istreambuf_iterator<char>());
        CHECK(limited_content.find("\"event_id\":\"sink-sync-disabled-by-limit-test-3\"") != std::string::npos);

        limited_sink.stop();
        fs::remove_all(limited_config.cache_path);
    }

    {
        lisysm::MonitorConfig failure_config;
        failure_config.device_id = "sink-sync-failure-test";
        failure_config.cache_path = "/tmp/minilisysm-jsonl-sink-sync-failure-test";
        failure_config.summary_enable = false;
        failure_config.critical_fsync = true;
        failure_config.max_fsync_per_minute = 1;
        failure_config.sink_idle_sleep_ms = 1;
        fs::remove_all(failure_config.cache_path);

        lisysm::JsonlEventSink failure_sink(failure_config);
        auto* failure_input = failure_sink.add_input_queue(failure_config.event_queue_capacity);
        CHECK(failure_input != nullptr);
        CHECK(failure_sink.start());

        fs::path failure_path;
        const fs::path jsonl_dir = fs::path(failure_config.cache_path) / "jsonl";
        for (int attempt = 0; attempt < 200 && failure_path.empty(); ++attempt) {
            if (fs::exists(jsonl_dir)) {
                for (const auto& entry : fs::directory_iterator(jsonl_dir)) {
                    if (entry.is_regular_file() && entry.path().extension() == ".jsonl") {
                        failure_path = entry.path();
                        break;
                    }
                }
            }
            if (failure_path.empty()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
        CHECK(!failure_path.empty());
        CHECK(::chmod(failure_path.c_str(), 0000) == 0);

        lisysm::InternalEvent critical_event;
        critical_event.event_type = lisysm::EventType::IoDelayRisk;
        critical_event.level = lisysm::EventLevel::Critical;
        critical_event.sequence = 4;
        CHECK(failure_input->push(critical_event, critical_event.level));
        failure_sink.stop();

        const lisysm::SinkStats failure_stats = failure_sink.stats();
        CHECK(failure_stats.written_events == 1);
        CHECK(failure_stats.fsync_count == 0);
        CHECK(failure_stats.fsync_failures == 1);
        CHECK(failure_stats.fsync_rate_limited == 0);
        CHECK(::chmod(failure_path.c_str(), 0600) == 0);
        fs::remove_all(failure_config.cache_path);
    }
#endif

    return EXIT_SUCCESS;
}
