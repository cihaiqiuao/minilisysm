#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace lisysm {

struct MonitorConfig {
    bool enable{true};
    std::string config_version{"default"};
    std::string platform{"unknown"};
    std::string software_version{"0.0.0"};
    std::string device_id{"unknown_device"};
    uint32_t fast_collect_interval_ms{1000};
    uint32_t low_freq_collect_interval_ms{10000};

    int fast_collector_cpu{-1};
    int persist_thread_cpu{-1};
    int fast_collector_nice{5};
    int background_nice{10};

    size_t event_queue_capacity{4096};
    size_t critical_reserved_slots{32};
    bool drop_info_when_full{true};
    bool drop_warning_when_full{true};

    bool memory_rule_enable{true};
    uint64_t mem_available_warning_mb{512};
    uint64_t mem_available_critical_mb{256};
    uint64_t mem_available_recovery_mb{768};
    uint32_t continuous_warning_windows{3};
    uint32_t continuous_critical_windows{2};
    uint32_t recovery_windows{3};
    uint32_t cooldown_sec{60};
    uint64_t self_rss_soft_limit_mb{40};
    uint64_t self_rss_hard_limit_mb{64};

    bool persistence_enable{true};
    std::string cache_path{"./lisysm_events"};
    uint64_t cache_max_mb{100};
    uint64_t file_rotate_mb{4};
    bool critical_fsync{true};
    uint32_t max_fsync_per_minute{6};
};

class ConfigLoader {
public:
    static MonitorConfig load_or_default(const std::string& path);
    static bool validate(MonitorConfig& config, std::string* error);
};

} // namespace lisysm
