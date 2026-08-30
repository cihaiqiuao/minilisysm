#include "minilisysm/core/config.hpp"
#include "minilisysm/runtime/monitor.hpp"

#include <spdlog/sinks/null_sink.h>
#include <spdlog/spdlog.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <thread>

#define CHECK(condition)                                                                                               \
    do {                                                                                                               \
        if (!(condition)) {                                                                                            \
            std::cerr << "check failed: " #condition << " at line " << __LINE__ << "\n";                               \
            return EXIT_FAILURE;                                                                                       \
        }                                                                                                              \
    } while (false)

namespace {

class TempDirectory {
  public:
    TempDirectory()
        : path_(std::filesystem::temp_directory_path() /
                ("minilisysm-monitor-test-" +
                 std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))) {}

    ~TempDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

    const std::filesystem::path& path() const {
        return path_;
    }

  private:
    std::filesystem::path path_;
};

} // namespace

int main() {
    auto logger = std::make_shared<spdlog::logger>("monitor_test", std::make_shared<spdlog::sinks::null_sink_mt>());
    spdlog::set_default_logger(logger);

    TempDirectory temp_directory;
    lisysm::MonitorConfig config;
    config.fast_collect_interval_ms = 10000;
    config.low_freq_collect_interval_ms = 10000;
    config.metrics_enable = false;
    config.persistence_enable = true;
    config.cache_path = temp_directory.path().string();
    config.summary_enable = false;
    config.network_sink_enable = false;
    config.memory_rule_enable = false;
    config.self_protection_enable = false;
    config.sched_delay_enable = false;
    config.io_delay_enable = false;
    config.cpu_usage_enable = false;

    {
        lisysm::Monitor monitor(config);
        CHECK(monitor.start());
        CHECK(!monitor.start());
        CHECK(monitor.running());

        // Give both collector threads time to finish their first lightweight pass
        // and enter the configured 10-second interval wait.
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        const auto started = std::chrono::steady_clock::now();
        monitor.stop();
        const auto elapsed = std::chrono::steady_clock::now() - started;
        CHECK(elapsed < std::chrono::seconds(1));
        CHECK(!monitor.running());

        monitor.stop();

        bool found_jsonl = false;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(temp_directory.path())) {
            if (entry.is_regular_file() && entry.path().extension() == ".jsonl" && entry.file_size() != 0) {
                found_jsonl = true;
                break;
            }
        }
        CHECK(found_jsonl);
    }

    spdlog::shutdown();
    return EXIT_SUCCESS;
}
