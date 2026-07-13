#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace lisysm {

struct MonitorConfig {
    bool enable{true};
    std::string config_version{"default"};
    std::string platform{"unknown"};
    std::string software_version{"0.0.0"};
    std::string device_id{"unknown_device"};
    uint32_t fast_collect_interval_ms{1000};
    uint32_t low_freq_collect_interval_ms{10000};
    uint32_t dispatcher_idle_sleep_ms{2};
    uint32_t sink_idle_sleep_ms{5};
    bool metrics_enable{true};
    std::string metrics_bind_host{"127.0.0.1"};
    uint16_t metrics_port{9108};
    bool metrics_scrape_runtime{true};
    bool metrics_scrape_collectors{true};
    bool metrics_scrape_rule_state{true};

    bool agent_log_enable{true};
    std::string agent_log_level{"info"};
    bool agent_log_console{true};
    std::string agent_log_path{"./logs/agent/minilisysm-agent.log"};
    std::string agent_log_rotation{"size"};
    uint64_t agent_log_rotate_mb{16};
    uint32_t agent_log_rotate_files{8};
    uint32_t agent_log_async_queue_size{8192};

    int fast_collector_cpu{-1};
    int sched_collector_cpu{-1};
    int persist_thread_cpu{-1};
    int fast_collector_nice{5};
    int sched_collector_nice{8};
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

    bool self_protection_enable{true};
    uint32_t queue_warning_percent{70};
    uint32_t queue_critical_percent{90};
    uint32_t queue_recovery_percent{50};
    uint32_t self_recovery_windows{3};
    uint64_t self_rss_soft_limit_mb{40};
    uint64_t self_rss_hard_limit_mb{64};
    uint64_t self_rss_recovery_mb{32};

    bool sched_delay_enable{true};
    std::string sched_delay_source{"proc"};
    std::vector<std::string> sched_process_whitelist{};
    std::vector<std::string> sched_thread_whitelist{};
    uint64_t sched_wait_sum_warning_us{10000};
    uint64_t sched_wait_sum_critical_us{30000};
    uint64_t sched_wait_sum_recovery_us{5000};
    uint64_t sched_ebpf_min_wait_us{10000};
    uint32_t sched_ebpf_ringbuf_kb{1024};
    uint32_t sched_ebpf_max_events_per_poll{4096};
    bool sched_ebpf_lifecycle_enable{true};
    uint32_t sched_ebpf_allowlist_refresh_ms{10000};
    bool sched_ebpf_aggregate_enable{false};
    uint32_t sched_ebpf_aggregate_window_ms{1000};
    uint32_t sched_ebpf_aggregate_max_entries{8192};
    uint32_t sched_proc_cache_refresh_ms{10000};
    uint32_t sched_proc_max_scan_threads{4096};
    uint32_t sched_collector_overrun_warning_ms{200};
    uint64_t sched_involuntary_switch_warning{100};
    uint32_t sched_continuous_warning_windows{3};
    uint32_t sched_continuous_critical_windows{2};
    uint32_t sched_recovery_windows{3};
    uint32_t sched_max_targets{32};

    bool process_memory_enable{true};
    uint64_t process_memory_growth_warning_mb{100};
    uint64_t process_memory_growth_critical_mb{200};
    uint64_t process_memory_growth_recovery_mb{20};
    uint32_t process_memory_growth_window_sec{600};

    bool io_delay_enable{true};
    std::vector<std::string> io_device_whitelist{};
    double io_await_warning_ms{50.0};
    double io_await_critical_ms{200.0};
    double io_await_recovery_ms{20.0};
    double io_util_warning_percent{80.0};
    double io_util_critical_percent{95.0};
    double io_util_recovery_percent{50.0};
    uint32_t io_continuous_warning_windows{3};
    uint32_t io_continuous_critical_windows{2};
    uint32_t io_recovery_windows{3};
    uint32_t io_max_targets{16};

    bool cpu_usage_enable{true};
    std::string cpu_usage_mode{"total"};
    std::vector<std::string> cpu_usage_core_whitelist{};
    double cpu_usage_warning_percent{80.0};
    double cpu_usage_critical_percent{95.0};
    double cpu_usage_recovery_percent{60.0};
    uint32_t cpu_usage_continuous_warning_windows{3};
    uint32_t cpu_usage_continuous_critical_windows{2};
    uint32_t cpu_usage_recovery_windows{3};

    bool persistence_enable{true};
    std::string cache_path{"./logs/events"};
    uint64_t cache_max_mb{100};
    uint64_t file_rotate_mb{4};
    bool critical_fsync{true};
    uint32_t max_fsync_per_minute{6};
    bool summary_enable{true};
    bool summary_color{false};

    bool network_sink_enable{false};
    std::string network_endpoint{"http://127.0.0.1:8080/events"};
    uint32_t network_batch_size{128};
    uint32_t network_flush_interval_ms{1000};
    uint32_t network_connect_timeout_ms{500};
    uint32_t network_request_timeout_ms{2000};
    uint32_t network_retry_base_ms{1000};
    uint32_t network_retry_max_ms{60000};
    std::string network_wal_path{"./logs/wal"};
    uint64_t network_wal_max_mb{64};
    uint64_t network_wal_segment_mb{4};
};

class ConfigLoader {
  public:
    static MonitorConfig load_or_default(const std::string& path);
    static bool validate(MonitorConfig& config, std::string* error);
};

} // namespace lisysm
