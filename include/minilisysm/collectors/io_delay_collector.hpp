#pragma once

#include "minilisysm/core/config.hpp"
#include "minilisysm/core/time.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace lisysm {

struct IoDelaySample {
    std::string device;
    uint64_t delta_io_count{0};
    double avg_await_ms{0.0};
    double util_percent{0.0};
    uint64_t in_flight{0};
    bool valid{false};
};

class IoDelayCollector {
  public:
    using Clock = uint64_t (*)();

    explicit IoDelayCollector(const MonitorConfig& config, std::string diskstats_path = "/proc/diskstats",
                              Clock clock = monotonic_ms);
    std::vector<IoDelaySample> collect();
    uint64_t last_failure_count() const {
        return last_failure_count_;
    }

  private:
    struct DiskStats {
        uint64_t read_ios{0};
        uint64_t read_time_ms{0};
        uint64_t write_ios{0};
        uint64_t write_time_ms{0};
        uint64_t in_flight{0};
        uint64_t io_time_ms{0};
        uint64_t timestamp_ms{0};
    };

    bool should_scan_device(const std::string& device) const;
    bool contains_name(const std::vector<std::string>& names, const std::string& value) const;
    std::unordered_map<std::string, DiskStats> read_diskstats() const;
    void prune_baselines(uint64_t now_ms);

    const MonitorConfig& config_;
    std::string diskstats_path_;
    std::unordered_map<std::string, DiskStats> baselines_;
    std::unordered_map<std::string, uint64_t> baseline_last_seen_ms_;
    Clock clock_;
    uint64_t last_failure_count_{0};
};

} // namespace lisysm
