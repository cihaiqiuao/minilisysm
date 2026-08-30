#pragma once

#include "minilisysm/core/config.hpp"
#include "minilisysm/core/time.hpp"
#include "minilisysm/interfaces/sched_delay_collector.hpp"

#include <chrono>
#include <string>
#include <unordered_map>

namespace lisysm {

class SchedDelayCollector : public SchedDelayCollectorInterface {
  public:
    using Clock = uint64_t (*)();

    explicit SchedDelayCollector(const MonitorConfig& config, std::string proc_dir = "/proc",
                                 Clock clock = monotonic_ms);
    std::vector<SchedDelaySample> collect() override;
    uint64_t last_failure_count() const override {
        return last_failure_count_;
    }

  private:
    struct Baseline {
        uint64_t wait_sum_us{0};
        uint64_t involuntary_switches{0};
        uint64_t last_seen_ms{0};
    };
    struct CommCacheEntry {
        std::string comm;
        std::chrono::steady_clock::time_point refreshed_at{};
        uint64_t last_seen_ms{0};
    };

    bool should_consider_process_id(int32_t pid) const;
    bool should_scan_process(int32_t pid, const std::string& comm) const;
    bool should_scan_thread(const std::string& comm) const;
    bool cached_comm(uint64_t key, const std::string& path, std::unordered_map<uint64_t, CommCacheEntry>& cache,
                     std::string* comm);
    bool read_comm(const std::string& path, std::string* comm) const;
    bool read_sched(int32_t pid, int32_t tid, uint64_t* wait_sum_us, uint64_t* involuntary_switches) const;
    bool contains_name(const std::vector<std::string>& names, const std::string& value) const;
    void prune_expired_state(uint64_t now_ms);

    const MonitorConfig& config_;
    std::string proc_dir_;
    std::unordered_map<uint64_t, Baseline> baselines_;
    std::unordered_map<uint64_t, CommCacheEntry> process_comm_cache_;
    std::unordered_map<uint64_t, CommCacheEntry> thread_comm_cache_;
    Clock clock_;
    uint64_t last_failure_count_{0};
};

} // namespace lisysm
