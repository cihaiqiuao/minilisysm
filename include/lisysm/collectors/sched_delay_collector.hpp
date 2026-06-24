#pragma once

#include "lisysm/core/config.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace lisysm {

struct SchedDelaySample {
    int32_t pid{-1};
    int32_t tid{-1};
    uint64_t delta_wait_sum_us{0};
    uint64_t delta_involuntary_switches{0};
    bool valid{false};
};

class SchedDelayCollector {
public:
    explicit SchedDelayCollector(const MonitorConfig& config);
    std::vector<SchedDelaySample> collect();
    uint64_t last_failure_count() const { return last_failure_count_; }

private:
    struct Baseline {
        uint64_t wait_sum_us{0};
        uint64_t involuntary_switches{0};
    };

    bool should_scan_process(int32_t pid, const std::string& comm) const;
    bool should_scan_thread(const std::string& comm) const;
    bool read_comm(const std::string& path, std::string* comm) const;
    bool read_sched(int32_t pid, int32_t tid, uint64_t* wait_sum_us, uint64_t* involuntary_switches) const;
    bool contains_name(const std::vector<std::string>& names, const std::string& value) const;

    const MonitorConfig& config_;
    std::unordered_map<uint64_t, Baseline> baselines_;
    uint64_t last_failure_count_{0};
};

} // namespace lisysm
