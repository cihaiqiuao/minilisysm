#include "minilisysm/collectors/cpu_usage_collector.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <string_view>
#include <utility>

namespace lisysm {

CpuUsageCollector::CpuUsageCollector(const MonitorConfig& config, std::string stat_path, Clock clock)
    : config_(config), stat_path_(std::move(stat_path)), clock_(clock != nullptr ? clock : monotonic_ms) {}

std::vector<CpuUsageSample> CpuUsageCollector::collect() {
    last_failure_count_ = 0;
    if (!config_.cpu_usage_enable) {
        return {};
    }

    const std::unordered_map<std::string, CpuStats> current = read_proc_stat();
    if (current.empty()) {
        ++last_failure_count_;
        return {};
    }

    const uint64_t now_ms = clock_();
    for (const auto& [cpu, stats] : current) {
        (void)stats;
        baseline_last_seen_ms_[cpu] = now_ms;
    }
    prune_baselines(now_ms);

    std::vector<CpuUsageSample> samples;
    samples.reserve(current.size());
    for (const auto& [cpu, stats] : current) {
        auto baseline = baselines_.find(cpu);
        if (baseline == baselines_.end()) {
            baselines_[cpu] = stats;
            continue;
        }

        const CpuStats previous = baseline->second;
        baseline->second = stats;
        const uint64_t current_total = total_jiffies(stats);
        const uint64_t previous_total = total_jiffies(previous);
        const uint64_t current_idle = idle_jiffies(stats);
        const uint64_t previous_idle = idle_jiffies(previous);

        if (current_total <= previous_total || current_idle < previous_idle) {
            continue;
        }

        CpuUsageSample sample;
        sample.cpu = cpu;
        sample.delta_total_jiffies = current_total - previous_total;
        sample.delta_idle_jiffies = current_idle - previous_idle;
        if (sample.delta_idle_jiffies > sample.delta_total_jiffies) {
            continue;
        }
        const uint64_t busy_jiffies = sample.delta_total_jiffies - sample.delta_idle_jiffies;
        sample.usage_percent = std::min(100.0, static_cast<double>(busy_jiffies) * 100.0 /
                                                   static_cast<double>(sample.delta_total_jiffies));
        sample.valid = true;
        samples.push_back(std::move(sample));
    }
    return samples;
}

void CpuUsageCollector::prune_baselines(uint64_t now_ms) {
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

uint64_t CpuUsageCollector::total_jiffies(const CpuStats& stats) {
    return stats.user + stats.nice + stats.system + stats.idle + stats.iowait + stats.irq + stats.softirq + stats.steal;
}

uint64_t CpuUsageCollector::idle_jiffies(const CpuStats& stats) {
    return stats.idle + stats.iowait;
}

bool CpuUsageCollector::should_scan_cpu(const std::string& cpu) const {
    if (cpu == "total") {
        return should_emit_total();
    }
    if (!should_emit_per_core()) {
        return false;
    }
    if (config_.cpu_usage_core_whitelist.empty()) {
        return true;
    }
    return std::find(config_.cpu_usage_core_whitelist.begin(), config_.cpu_usage_core_whitelist.end(), cpu) !=
           config_.cpu_usage_core_whitelist.end();
}

bool CpuUsageCollector::should_emit_total() const {
    return config_.cpu_usage_mode == "total" || config_.cpu_usage_mode == "both";
}

bool CpuUsageCollector::should_emit_per_core() const {
    return config_.cpu_usage_mode == "per_core" || config_.cpu_usage_mode == "both";
}

std::unordered_map<std::string, CpuUsageCollector::CpuStats> CpuUsageCollector::read_proc_stat() const {
    std::ifstream input(stat_path_);
    if (!input) {
        return {};
    }

    std::unordered_map<std::string, CpuStats> cpus;
    std::string line;
    while (std::getline(input, line)) {
        std::istringstream stream(line);
        std::string label;
        CpuStats stats;
        if (!(stream >> label)) {
            continue;
        }
        if (label == "cpu") {
            label = "total";
        } else if (std::string_view(label).rfind("cpu", 0) != 0) {
            continue;
        }
        if (!should_scan_cpu(label)) {
            continue;
        }
        if (!(stream >> stats.user >> stats.nice >> stats.system >> stats.idle)) {
            continue;
        }
        stream >> stats.iowait >> stats.irq >> stats.softirq >> stats.steal >> stats.guest >> stats.guest_nice;
        cpus.emplace(std::move(label), stats);
    }
    return cpus;
}

} // namespace lisysm
