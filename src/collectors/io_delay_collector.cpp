#include "minilisysm/collectors/io_delay_collector.hpp"
#include "minilisysm/core/time.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <string_view>

namespace lisysm {
namespace {

bool starts_with(const std::string& value, const char* prefix) {
    const std::string_view view(value);
    const std::string_view wanted(prefix);
    return view.size() >= wanted.size() && view.substr(0, wanted.size()) == wanted;
}

} // namespace

IoDelayCollector::IoDelayCollector(const MonitorConfig& config, std::string diskstats_path, Clock clock)
    : config_(config), diskstats_path_(std::move(diskstats_path)), clock_(clock ? clock : monotonic_ms) {}

std::vector<IoDelaySample> IoDelayCollector::collect() {
    last_failure_count_ = 0;
    if (!config_.io_delay_enable) {
        return {};
    }

    const std::unordered_map<std::string, DiskStats> current = read_diskstats();
    if (current.empty()) {
        ++last_failure_count_;
        return {};
    }

    const uint64_t now_ms = clock_();
    for (const auto& [device, stats] : current) {
        (void)stats;
        baseline_last_seen_ms_[device] = now_ms;
    }
    prune_baselines(now_ms);

    std::vector<IoDelaySample> samples;
    samples.reserve(std::min<size_t>(current.size(), config_.io_max_targets));
    for (const auto& [device, stats] : current) {
        auto baseline = baselines_.find(device);
        if (baseline == baselines_.end()) {
            baselines_[device] = stats;
            continue;
        }

        const DiskStats previous = baseline->second;
        baseline->second = stats;
        if (stats.timestamp_ms <= previous.timestamp_ms) {
            continue;
        }
        if (stats.read_ios < previous.read_ios || stats.write_ios < previous.write_ios ||
            stats.read_time_ms < previous.read_time_ms || stats.write_time_ms < previous.write_time_ms ||
            stats.io_time_ms < previous.io_time_ms) {
            continue;
        }
        const uint64_t delta_read_ios = stats.read_ios - previous.read_ios;
        const uint64_t delta_write_ios = stats.write_ios - previous.write_ios;
        const uint64_t delta_ios = delta_read_ios + delta_write_ios;
        if (delta_ios == 0) {
            continue;
        }

        const uint64_t delta_read_time = stats.read_time_ms - previous.read_time_ms;
        const uint64_t delta_write_time = stats.write_time_ms - previous.write_time_ms;
        const uint64_t delta_io_time = stats.io_time_ms - previous.io_time_ms;
        const uint64_t elapsed_ms = stats.timestamp_ms - previous.timestamp_ms;

        IoDelaySample sample;
        sample.device = device;
        sample.delta_io_count = delta_ios;
        sample.avg_await_ms = static_cast<double>(delta_read_time + delta_write_time) / static_cast<double>(delta_ios);
        sample.util_percent =
            std::min(100.0, static_cast<double>(delta_io_time) * 100.0 / static_cast<double>(elapsed_ms));
        sample.in_flight = stats.in_flight;
        sample.valid = true;
        samples.push_back(sample);
        if (samples.size() >= config_.io_max_targets) {
            break;
        }
    }
    return samples;
}

void IoDelayCollector::prune_baselines(uint64_t now_ms) {
    const uint64_t ttl_sec = config_.state_ttl_sec == 0 ? 3600 : config_.state_ttl_sec;
    const uint64_t ttl_ms = ttl_sec * 1000ULL;
    for (auto it = baseline_last_seen_ms_.begin(); it != baseline_last_seen_ms_.end();) {
        if (now_ms - it->second >= ttl_ms) {
            baselines_.erase(it->first);
            it = baseline_last_seen_ms_.erase(it);
        } else {
            ++it;
        }
    }
}

bool IoDelayCollector::should_scan_device(const std::string& device) const {
    if (!config_.io_device_whitelist.empty()) {
        return contains_name(config_.io_device_whitelist, device);
    }
    return !starts_with(device, "loop") && !starts_with(device, "ram") && !starts_with(device, "fd");
}

bool IoDelayCollector::contains_name(const std::vector<std::string>& names, const std::string& value) const {
    return std::find(names.begin(), names.end(), value) != names.end();
}

std::unordered_map<std::string, IoDelayCollector::DiskStats> IoDelayCollector::read_diskstats() const {
    std::ifstream input(diskstats_path_);
    if (!input) {
        return {};
    }

    std::unordered_map<std::string, DiskStats> devices;
    std::string line;
    const uint64_t now = clock_();
    while (std::getline(input, line)) {
        std::istringstream stream(line);
        uint32_t major = 0;
        uint32_t minor = 0;
        std::string device;
        DiskStats stats;
        uint64_t read_merges = 0;
        uint64_t read_sectors = 0;
        uint64_t write_merges = 0;
        uint64_t write_sectors = 0;

        if (!(stream >> major >> minor >> device >> stats.read_ios >> read_merges >> read_sectors >>
              stats.read_time_ms >> stats.write_ios >> write_merges >> write_sectors >> stats.write_time_ms >>
              stats.in_flight >> stats.io_time_ms)) {
            continue;
        }
        (void)major;
        (void)minor;
        if (!should_scan_device(device)) {
            continue;
        }
        stats.timestamp_ms = now;
        devices.emplace(std::move(device), stats);
    }
    return devices;
}

} // namespace lisysm
