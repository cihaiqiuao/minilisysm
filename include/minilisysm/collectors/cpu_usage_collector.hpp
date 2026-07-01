#pragma once

#include "minilisysm/core/config.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace lisysm {

struct CpuUsageSample {
    std::string cpu{"total"};
    double usage_percent{0.0};
    uint64_t delta_total_jiffies{0};
    uint64_t delta_idle_jiffies{0};
    bool valid{false};
};

class CpuUsageCollector {
  public:
    explicit CpuUsageCollector(const MonitorConfig& config, std::string stat_path = "/proc/stat");
    std::vector<CpuUsageSample> collect();
    uint64_t last_failure_count() const {
        return last_failure_count_;
    }

  private:
    struct CpuStats {
        uint64_t user{0};
        uint64_t nice{0};
        uint64_t system{0};
        uint64_t idle{0};
        uint64_t iowait{0};
        uint64_t irq{0};
        uint64_t softirq{0};
        uint64_t steal{0};
        uint64_t guest{0};
        uint64_t guest_nice{0};
    };

    static uint64_t total_jiffies(const CpuStats& stats);
    static uint64_t idle_jiffies(const CpuStats& stats);
    bool should_scan_cpu(const std::string& cpu) const;
    bool should_emit_total() const;
    bool should_emit_per_core() const;
    std::unordered_map<std::string, CpuStats> read_proc_stat() const;

    const MonitorConfig& config_;
    std::string stat_path_;
    std::unordered_map<std::string, CpuStats> baselines_;
    uint64_t last_failure_count_{0};
};

} // namespace lisysm
