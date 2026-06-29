#include "minilisysm/core/config.hpp"
#include "minilisysm/runtime/monitor.hpp"

#include <spdlog/async.h>
#include <spdlog/sinks/daily_file_sink.h>
#include <spdlog/sinks/null_sink.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <filesystem>
#include <iterator>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {
#ifndef MINILISYSM_DEFAULT_CONFIG_PATH
#define MINILISYSM_DEFAULT_CONFIG_PATH "configs/lisysm_monitor.ini"
#endif

std::atomic<bool>& stop_requested() {
    static std::atomic<bool> value{false};
    return value;
}

void handle_signal(int) {
    stop_requested().store(true);
}

const char* enabled(bool value) {
    return value ? "enabled" : "disabled";
}

spdlog::level::level_enum parse_level(const std::string& level) {
    if (level == "debug") {
        return spdlog::level::debug;
    }
    if (level == "warn") {
        return spdlog::level::warn;
    }
    if (level == "error") {
        return spdlog::level::err;
    }
    return spdlog::level::info;
}

void init_bootstrap_logger() {
    auto logger = spdlog::stderr_color_mt("minilisysm_bootstrap");
    logger->set_pattern("%Y-%m-%d %H:%M:%S.%e [%l] [%t] %v");
    spdlog::set_default_logger(logger);
    spdlog::set_level(spdlog::level::info);
    spdlog::flush_on(spdlog::level::warn);
}

void init_fallback_logger() {
    spdlog::drop_all();
    init_bootstrap_logger();
}

void init_agent_logger(const lisysm::MonitorConfig& config) {
    try {
        std::vector<spdlog::sink_ptr> sinks{};
        if (config.agent_log_console) {
            sinks.push_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());
        }
        if (config.agent_log_enable) {
            const std::filesystem::path log_path(config.agent_log_path);
            if (log_path.has_parent_path()) {
                std::filesystem::create_directories(log_path.parent_path());
            }
            const size_t max_size = static_cast<size_t>(config.agent_log_rotate_mb) * 1024U * 1024U;
            const bool use_daily_rotation = config.agent_log_rotation == "daily";
            if (use_daily_rotation) {
                sinks.push_back(std::make_shared<spdlog::sinks::daily_file_sink_mt>(config.agent_log_path, 0, 0, false,
                                                                                    config.agent_log_rotate_files));
            }
            if (!use_daily_rotation) {
                sinks.push_back(std::make_shared<spdlog::sinks::rotating_file_sink_mt>(config.agent_log_path, max_size,
                                                                                       config.agent_log_rotate_files));
            }
        }
        if (sinks.empty()) {
            sinks.push_back(std::make_shared<spdlog::sinks::null_sink_mt>());
        }

        spdlog::drop_all();
        spdlog::init_thread_pool(config.agent_log_async_queue_size, 1);
        auto logger =
            std::make_shared<spdlog::async_logger>("minilisysm", sinks.begin(), sinks.end(), spdlog::thread_pool(),
                                                   spdlog::async_overflow_policy::overrun_oldest);
        logger->set_pattern("%Y-%m-%d %H:%M:%S.%e [%l] [%t] %v");
        logger->set_level(parse_level(config.agent_log_level));
        logger->flush_on(spdlog::level::warn);
        spdlog::register_logger(logger);
        spdlog::set_default_logger(logger);
        spdlog::set_level(parse_level(config.agent_log_level));
    } catch (const spdlog::spdlog_ex& ex) {
        init_fallback_logger();
        spdlog::error("failed to initialize spdlog file logger: {}", ex.what());
    } catch (const std::exception& ex) {
        init_fallback_logger();
        spdlog::error("failed to initialize logger: {}", ex.what());
    }
}

void log_config_summary(const lisysm::MonitorConfig& config, const std::string& config_path) {
    spdlog::info("config loaded: path={} version={} platform={} device_id={}", config_path, config.config_version,
                 config.platform, config.device_id);
    spdlog::info("runtime: fast_collect_interval_ms={} low_freq_collect_interval_ms={} event_queue_capacity={}",
                 config.fast_collect_interval_ms, config.low_freq_collect_interval_ms, config.event_queue_capacity);
    spdlog::info("rules: memory={} self_protection={} sched_delay={} sched_source={} io_delay={}",
                 enabled(config.memory_rule_enable), enabled(config.self_protection_enable),
                 enabled(config.sched_delay_enable), config.sched_delay_source, enabled(config.io_delay_enable));
    spdlog::info("persistence: {} cache_path={} cache_max_mb={} file_rotate_mb={}", enabled(config.persistence_enable),
                 config.cache_path, config.cache_max_mb, config.file_rotate_mb);
    spdlog::info("agent_log: {} level={} console={} path={} rotation={} rotate_mb={} rotate_files={} async_queue={}",
                 enabled(config.agent_log_enable), config.agent_log_level, enabled(config.agent_log_console),
                 config.agent_log_path, config.agent_log_rotation, config.agent_log_rotate_mb,
                 config.agent_log_rotate_files, config.agent_log_async_queue_size);
    spdlog::info("metrics: {} endpoint=http://{}:{}/metrics", enabled(config.metrics_enable), config.metrics_bind_host,
                 config.metrics_port);
    spdlog::info("network_sink: {} endpoint={} wal_path={}", enabled(config.network_sink_enable),
                 config.network_endpoint, config.network_wal_path);
}
} // namespace

int main(int argc, char** argv) {
    init_bootstrap_logger();
    const std::string config_path = argc > 1 ? std::string(*std::next(argv)) : MINILISYSM_DEFAULT_CONFIG_PATH;
    spdlog::info("minilisysm starting");
    lisysm::MonitorConfig config = lisysm::ConfigLoader::load_or_default(config_path);
    init_agent_logger(config);
    std::string error;
    lisysm::ConfigLoader::validate(config, &error);
    if (!error.empty()) {
        spdlog::warn("config warning: {}", error);
    }
    log_config_summary(config, config_path);

    (void)stop_requested();
    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    lisysm::Monitor monitor(config);
    if (!monitor.start()) {
        spdlog::error("monitor disabled or failed to start");
        return 1;
    }
    spdlog::info("monitor started successfully");

    while (!stop_requested().load()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    spdlog::info("stop signal received");
    monitor.stop();
    spdlog::info("monitor stopped");
    spdlog::shutdown();
    return 0;
}
