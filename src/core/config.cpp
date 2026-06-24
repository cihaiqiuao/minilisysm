#include "lisysm/core/config.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <vector>

namespace lisysm {
namespace {

using Section = std::unordered_map<std::string, std::string>;
using Ini = std::unordered_map<std::string, Section>;

std::string trim(std::string_view input)
{
    size_t begin = 0;
    size_t end = input.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(input[begin]))) {
        ++begin;
    }
    while (end > begin && std::isspace(static_cast<unsigned char>(input[end - 1]))) {
        --end;
    }
    return std::string(input.substr(begin, end - begin));
}

Ini parse_ini(const std::string& path)
{
    std::ifstream stream(path);
    Ini ini;
    std::string section;
    std::string line;
    while (std::getline(stream, line)) {
        const auto comment = line.find_first_of("#;");
        if (comment != std::string::npos) {
            line.resize(comment);
        }
        const std::string cleaned = trim(line);
        if (cleaned.empty()) {
            continue;
        }
        if (cleaned.front() == '[' && cleaned.back() == ']') {
            section = trim(std::string_view(cleaned).substr(1, cleaned.size() - 2));
            continue;
        }
        const auto equals = cleaned.find('=');
        if (equals == std::string::npos || section.empty()) {
            continue;
        }
        ini[section][trim(std::string_view(cleaned).substr(0, equals))] =
            trim(std::string_view(cleaned).substr(equals + 1));
    }
    return ini;
}

const std::string* find_value(const Ini& ini, const char* section, const char* key)
{
    const auto sit = ini.find(section);
    if (sit == ini.end()) {
        return nullptr;
    }
    const auto kit = sit->second.find(key);
    if (kit == sit->second.end()) {
        return nullptr;
    }
    return &kit->second;
}

bool parse_bool(std::string_view value)
{
    std::string lower(value);
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return lower == "true" || lower == "1" || lower == "yes" || lower == "on";
}

template <typename T>
void assign_int(const Ini& ini, const char* section, const char* key, T& target)
{
    const std::string* value = find_value(ini, section, key);
    if (!value) {
        return;
    }
    T parsed{};
    const char* begin = value->data();
    const char* end = begin + value->size();
    if (std::from_chars(begin, end, parsed).ec == std::errc{}) {
        target = parsed;
    }
}

void assign_bool(const Ini& ini, const char* section, const char* key, bool& target)
{
    const std::string* value = find_value(ini, section, key);
    if (value) {
        target = parse_bool(*value);
    }
}

void assign_string(const Ini& ini, const char* section, const char* key, std::string& target)
{
    const std::string* value = find_value(ini, section, key);
    if (value) {
        target = *value;
    }
}

std::vector<std::string> split_csv(std::string_view value)
{
    std::vector<std::string> result;
    while (!value.empty()) {
        const size_t comma = value.find(',');
        std::string item = comma == std::string_view::npos ? trim(value) : trim(value.substr(0, comma));
        if (!item.empty()) {
            result.push_back(item);
        }
        if (comma == std::string_view::npos) {
            break;
        }
        value.remove_prefix(comma + 1);
    }
    return result;
}

void assign_csv(const Ini& ini, const char* section, const char* key, std::vector<std::string>& target)
{
    const std::string* value = find_value(ini, section, key);
    if (value) {
        target = split_csv(*value);
    }
}

} // namespace

MonitorConfig ConfigLoader::load_or_default(const std::string& path)
{
    MonitorConfig config;
    const Ini ini = parse_ini(path);

    assign_bool(ini, "linux_stability_monitor", "enable", config.enable);
    assign_string(ini, "linux_stability_monitor", "config_version", config.config_version);
    assign_string(ini, "linux_stability_monitor", "platform", config.platform);
    assign_string(ini, "linux_stability_monitor", "software_version", config.software_version);
    assign_string(ini, "linux_stability_monitor", "device_id", config.device_id);
    assign_int(ini, "linux_stability_monitor", "fast_collect_interval_ms", config.fast_collect_interval_ms);
    assign_int(ini, "linux_stability_monitor", "low_freq_collect_interval_ms", config.low_freq_collect_interval_ms);

    assign_int(ini, "thread_policy", "fast_collector_cpu", config.fast_collector_cpu);
    assign_int(ini, "thread_policy", "sched_collector_cpu", config.sched_collector_cpu);
    assign_int(ini, "thread_policy", "persist_thread_cpu", config.persist_thread_cpu);
    assign_int(ini, "thread_policy", "fast_collector_nice", config.fast_collector_nice);
    assign_int(ini, "thread_policy", "sched_collector_nice", config.sched_collector_nice);
    assign_int(ini, "thread_policy", "background_nice", config.background_nice);

    assign_int(ini, "event_queue", "capacity", config.event_queue_capacity);
    assign_int(ini, "event_queue", "critical_reserved_slots", config.critical_reserved_slots);
    assign_bool(ini, "event_queue", "drop_info_when_full", config.drop_info_when_full);
    assign_bool(ini, "event_queue", "drop_warning_when_full", config.drop_warning_when_full);

    assign_bool(ini, "memory_rule", "enable", config.memory_rule_enable);
    assign_int(ini, "memory_rule", "mem_available_warning_mb", config.mem_available_warning_mb);
    assign_int(ini, "memory_rule", "mem_available_critical_mb", config.mem_available_critical_mb);
    assign_int(ini, "memory_rule", "mem_available_recovery_mb", config.mem_available_recovery_mb);
    assign_int(ini, "memory_rule", "continuous_warning_windows", config.continuous_warning_windows);
    assign_int(ini, "memory_rule", "continuous_critical_windows", config.continuous_critical_windows);
    assign_int(ini, "memory_rule", "recovery_windows", config.recovery_windows);
    assign_int(ini, "memory_rule", "cooldown_sec", config.cooldown_sec);

    assign_bool(ini, "self_protection", "enable", config.self_protection_enable);
    assign_int(ini, "self_protection", "queue_warning_percent", config.queue_warning_percent);
    assign_int(ini, "self_protection", "queue_critical_percent", config.queue_critical_percent);
    assign_int(ini, "self_protection", "queue_recovery_percent", config.queue_recovery_percent);
    assign_int(ini, "self_protection", "self_recovery_windows", config.self_recovery_windows);
    assign_int(ini, "self_protection", "self_rss_soft_limit_mb", config.self_rss_soft_limit_mb);
    assign_int(ini, "self_protection", "self_rss_hard_limit_mb", config.self_rss_hard_limit_mb);
    assign_int(ini, "self_protection", "self_rss_recovery_mb", config.self_rss_recovery_mb);

    assign_bool(ini, "sched_delay_rule", "enable", config.sched_delay_enable);
    assign_csv(ini, "sched_delay_rule", "process_whitelist", config.sched_process_whitelist);
    assign_csv(ini, "sched_delay_rule", "thread_whitelist", config.sched_thread_whitelist);
    assign_int(ini, "sched_delay_rule", "wait_sum_warning_us", config.sched_wait_sum_warning_us);
    assign_int(ini, "sched_delay_rule", "wait_sum_critical_us", config.sched_wait_sum_critical_us);
    assign_int(ini, "sched_delay_rule", "wait_sum_recovery_us", config.sched_wait_sum_recovery_us);
    assign_int(ini, "sched_delay_rule", "involuntary_switch_warning", config.sched_involuntary_switch_warning);
    assign_int(ini, "sched_delay_rule", "continuous_warning_windows", config.sched_continuous_warning_windows);
    assign_int(ini, "sched_delay_rule", "continuous_critical_windows", config.sched_continuous_critical_windows);
    assign_int(ini, "sched_delay_rule", "recovery_windows", config.sched_recovery_windows);
    assign_int(ini, "sched_delay_rule", "max_targets", config.sched_max_targets);

    assign_bool(ini, "persistence", "enable", config.persistence_enable);
    assign_string(ini, "persistence", "cache_path", config.cache_path);
    assign_int(ini, "persistence", "cache_max_mb", config.cache_max_mb);
    assign_int(ini, "persistence", "file_rotate_mb", config.file_rotate_mb);
    assign_bool(ini, "persistence", "critical_fsync", config.critical_fsync);
    assign_int(ini, "persistence", "max_fsync_per_minute", config.max_fsync_per_minute);

    std::string ignored;
    validate(config, &ignored);
    return config;
}

bool ConfigLoader::validate(MonitorConfig& config, std::string* error)
{
    if (config.fast_collect_interval_ms < 100) {
        config.fast_collect_interval_ms = 100;
    }
    if (config.event_queue_capacity < 16) {
        config.event_queue_capacity = 16;
    }
    if (config.mem_available_critical_mb > config.mem_available_warning_mb) {
        if (error) {
            *error = "memory critical threshold must be lower than warning threshold";
        }
        config.memory_rule_enable = false;
        return false;
    }
    if (config.mem_available_recovery_mb < config.mem_available_warning_mb) {
        config.mem_available_recovery_mb = config.mem_available_warning_mb;
    }
    if (config.queue_critical_percent < config.queue_warning_percent) {
        config.queue_critical_percent = config.queue_warning_percent;
    }
    if (config.queue_recovery_percent > config.queue_warning_percent) {
        config.queue_recovery_percent = config.queue_warning_percent;
    }
    if (config.self_rss_hard_limit_mb < config.self_rss_soft_limit_mb) {
        config.self_rss_hard_limit_mb = config.self_rss_soft_limit_mb;
    }
    if (config.self_rss_recovery_mb > config.self_rss_soft_limit_mb) {
        config.self_rss_recovery_mb = config.self_rss_soft_limit_mb;
    }
    if (config.sched_wait_sum_critical_us < config.sched_wait_sum_warning_us) {
        config.sched_wait_sum_critical_us = config.sched_wait_sum_warning_us;
    }
    if (config.sched_wait_sum_recovery_us > config.sched_wait_sum_warning_us) {
        config.sched_wait_sum_recovery_us = config.sched_wait_sum_warning_us;
    }
    if (config.sched_max_targets == 0) {
        config.sched_max_targets = 1;
    }
    if (config.cache_max_mb == 0) {
        config.cache_max_mb = 1;
    }
    if (config.file_rotate_mb == 0 || config.file_rotate_mb > config.cache_max_mb) {
        config.file_rotate_mb = std::max<uint64_t>(1, std::min<uint64_t>(4, config.cache_max_mb));
    }
    return true;
}

} // namespace lisysm
