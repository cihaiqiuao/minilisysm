#pragma once

#include <cstdint>
#include <vector>

namespace lisysm {

struct SchedDelaySample {
    int32_t pid{-1};
    int32_t tid{-1};
    uint64_t delta_wait_sum_us{0};
    uint64_t delta_involuntary_switches{0};
    uint64_t max_wait_us{0};
    uint64_t avg_wait_us{0};
    uint64_t aggregate_count{0};
    bool valid{false};
};

struct SchedDelayCollectorRuntimeStats {
    uint64_t ebpf_ringbuf_drops{0};
    uint64_t ebpf_allowlist_exec_seen{0};
    uint64_t ebpf_allowlist_exit_cleaned{0};
    uint64_t ebpf_allowlist_stale_hits{0};
    uint64_t ebpf_aggregate_drops{0};
    uint64_t allowlist_scanned_processes{0};
    uint64_t allowlist_matched_pids{0};
    uint64_t allowlist_matched_tids{0};
    uint64_t allowlist_refresh_elapsed_ms{0};
};

class SchedDelayCollectorInterface {
public:
    virtual ~SchedDelayCollectorInterface() = default;
    virtual std::vector<SchedDelaySample> collect() = 0;
    virtual uint64_t last_failure_count() const = 0;
    virtual SchedDelayCollectorRuntimeStats runtime_stats() const { return {}; }
};

} // namespace lisysm
