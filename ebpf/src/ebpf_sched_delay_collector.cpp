#include "minilisysm/ebpf/ebpf_sched_delay_collector.hpp"

#include "sched_delay.skel.h"

#include <bpf/bpf.h>
#include <bpf/libbpf.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <filesystem>
#include <mutex>
#include <string>
#include <system_error>
#include <unordered_set>

namespace lisysm {
namespace {

struct SchedDelayBpfEvent {
    uint32_t pid;
    uint32_t tid;
    uint64_t delta_wait_ns;
    uint64_t involuntary_switches;
    uint64_t max_wait_ns;
    uint64_t avg_wait_ns;
    uint64_t aggregate_count;
    uint32_t flags;
};

struct SchedDelayBpfConfig {
    uint64_t min_wait_ns;
    uint64_t aggregate_window_ns;
    uint32_t max_events_per_poll;
    uint32_t enable_pid_filter;
    uint32_t enable_tid_filter;
    uint32_t enable_lifecycle;
    uint32_t enable_aggregate;
    uint32_t aggregate_max_entries;
};

struct SchedDelayBpfCounters {
    uint64_t ringbuf_drops;
    uint64_t allowlist_exec_seen;
    uint64_t allowlist_exit_cleaned;
    uint64_t allowlist_stale_hits;
    uint64_t aggregate_drops;
};

std::string trim_newline(std::string value)
{
    while (!value.empty() && (value.back() == '\n' || value.back() == '\r')) {
        value.pop_back();
    }
    return value;
}

bool read_comm(const std::string& path, std::string* comm)
{
    std::ifstream input(path);
    if (!input) {
        return false;
    }
    std::string line;
    std::getline(input, line);
    *comm = trim_newline(line);
    return !comm->empty();
}

bool contains_name(const std::vector<std::string>& names, const std::string& value)
{
    return std::find(names.begin(), names.end(), value) != names.end();
}

bool parse_i32(const std::string& text, int32_t* value)
{
    try {
        size_t parsed_chars = 0;
        const int parsed = std::stoi(text, &parsed_chars);
        if (parsed_chars != text.size()) {
            return false;
        }
        *value = static_cast<int32_t>(parsed);
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace

struct EbpfSchedDelayCollector::Impl {
    explicit Impl(EbpfSchedDelayCollector* owner) : owner(owner) {}

    ~Impl()
    {
        if (ring_buffer_) {
            ring_buffer__free(ring_buffer_);
        }
        if (skel) {
            sched_delay_bpf__destroy(skel);
        }
    }

    bool init()
    {
        libbpf_set_strict_mode(LIBBPF_STRICT_ALL);
        skel = sched_delay_bpf__open();
        if (!skel) {
            return false;
        }
        if (sched_delay_bpf__load(skel) != 0) {
            return false;
        }
        if (!configure_maps()) {
            return false;
        }
        if (sched_delay_bpf__attach(skel) != 0) {
            return false;
        }
        ring_buffer_ = ring_buffer__new(bpf_map__fd(skel->maps.events), &Impl::handle_event, this, nullptr);
        return ring_buffer_ != nullptr;
    }

    bool configure_maps()
    {
        const uint32_t key = 0;
        const SchedDelayBpfConfig config{
            owner->config_.sched_ebpf_min_wait_us * 1000ULL,
            owner->config_.sched_ebpf_aggregate_window_ms * 1000000ULL,
            owner->config_.sched_ebpf_max_events_per_poll,
            owner->config_.sched_process_whitelist.empty() ? 0U : 1U,
            owner->config_.sched_thread_whitelist.empty() ? 0U : 1U,
            owner->config_.sched_ebpf_lifecycle_enable ? 1U : 0U,
            owner->config_.sched_ebpf_aggregate_enable ? 1U : 0U,
            owner->config_.sched_ebpf_aggregate_max_entries,
        };
        if (bpf_map_update_elem(bpf_map__fd(skel->maps.bpf_config), &key, &config, BPF_ANY) != 0) {
            return false;
        }
        const SchedDelayBpfCounters counters{};
        if (bpf_map_update_elem(bpf_map__fd(skel->maps.counters), &key, &counters, BPF_ANY) != 0) {
            return false;
        }
        return refresh_allowlists();
    }

    bool refresh_allowlists()
    {
        const auto refresh_start = std::chrono::steady_clock::now();
        uint64_t scanned_processes = 0;
        uint64_t matched_pids = 0;
        uint64_t matched_tids = 0;
        clear_u32_map(bpf_map__fd(skel->maps.pid_allowlist));
        clear_u32_map(bpf_map__fd(skel->maps.tid_allowlist));

        if (owner->config_.sched_process_whitelist.empty() &&
            owner->config_.sched_thread_whitelist.empty()) {
            update_refresh_stats(refresh_start, scanned_processes, matched_pids, matched_tids);
            return true;
        }

        namespace fs = std::filesystem;
        std::error_code proc_ec;
        const fs::directory_iterator proc_iter("/proc", proc_ec);
        if (proc_ec) {
            return false;
        }
        const uint8_t allowed = 1;
        for (const auto& proc : proc_iter) {
            if (!proc.is_directory()) {
                continue;
            }
            ++scanned_processes;
            int32_t pid = -1;
            if (!parse_i32(proc.path().filename().string(), &pid)) {
                continue;
            }
            std::string process_comm;
            const bool process_known = read_comm((proc.path() / "comm").string(), &process_comm);
            const bool process_allowed =
                owner->config_.sched_process_whitelist.empty() ||
                (process_known && contains_name(owner->config_.sched_process_whitelist, process_comm));
            if (!process_allowed) {
                continue;
            }
            if (!owner->config_.sched_process_whitelist.empty()) {
                const uint32_t key = static_cast<uint32_t>(pid);
                bpf_map_update_elem(bpf_map__fd(skel->maps.pid_allowlist), &key, &allowed, BPF_ANY);
                ++matched_pids;
            }
            if (owner->config_.sched_thread_whitelist.empty()) {
                continue;
            }
            std::error_code task_ec;
            const fs::directory_iterator task_iter(proc.path() / "task", task_ec);
            if (task_ec) {
                continue;
            }
            for (const auto& task : task_iter) {
                int32_t tid = -1;
                if (!parse_i32(task.path().filename().string(), &tid)) {
                    continue;
                }
                std::string thread_comm;
                if (!read_comm((task.path() / "comm").string(), &thread_comm) ||
                    !contains_name(owner->config_.sched_thread_whitelist, thread_comm)) {
                    continue;
                }
                const uint32_t key = static_cast<uint32_t>(tid);
                bpf_map_update_elem(bpf_map__fd(skel->maps.tid_allowlist), &key, &allowed, BPF_ANY);
                ++matched_tids;
            }
        }
        update_refresh_stats(refresh_start, scanned_processes, matched_pids, matched_tids);
        return true;
    }

    void update_refresh_stats(
        std::chrono::steady_clock::time_point refresh_start,
        uint64_t scanned_processes,
        uint64_t matched_pids,
        uint64_t matched_tids)
    {
        std::lock_guard<std::mutex> lock(runtime_stats_mutex_);
        runtime_stats.allowlist_scanned_processes = scanned_processes;
        runtime_stats.allowlist_matched_pids = matched_pids;
        runtime_stats.allowlist_matched_tids = matched_tids;
        runtime_stats.allowlist_refresh_elapsed_ms = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - refresh_start)
                .count());
    }

    std::vector<SchedDelaySample> poll()
    {
        pending.clear();
        if (!ring_buffer_) {
            return {};
        }
        const auto now = std::chrono::steady_clock::now();
        const auto refresh = std::chrono::milliseconds(owner->config_.sched_ebpf_allowlist_refresh_ms);
        const SchedDelayBpfCounters counters = read_counters();
        const uint64_t exec_delta = counters.allowlist_exec_seen >= last_exec_seen
            ? counters.allowlist_exec_seen - last_exec_seen
            : 0;
        last_exec_seen = counters.allowlist_exec_seen;
        if ((!owner->config_.sched_process_whitelist.empty() ||
             !owner->config_.sched_thread_whitelist.empty()) &&
            (now - last_filter_refresh >= refresh || exec_delta > 0)) {
            refresh_allowlists();
            last_filter_refresh = now;
        }
        const int rc = ring_buffer__poll(ring_buffer_, 0);
        if (rc < 0 && rc != -EINTR && rc != 1) {
            ++poll_failures;
        }
        poll_failures += read_counter_delta(counters);
        return pending;
    }

    static int handle_event(void* ctx, void* data, size_t size)
    {
        if (size < sizeof(SchedDelayBpfEvent)) {
            return 0;
        }
        auto* self = static_cast<Impl*>(ctx);
        const auto* event = static_cast<const SchedDelayBpfEvent*>(data);
        return self->append_sample(*event);
    }

    int append_sample(const SchedDelayBpfEvent& event)
    {
        if (pending.size() >= owner->config_.sched_ebpf_max_events_per_poll) {
            return 1;
        }
        SchedDelaySample sample;
        sample.pid = static_cast<int32_t>(event.pid);
        sample.tid = static_cast<int32_t>(event.tid);
        sample.delta_wait_sum_us = event.delta_wait_ns / 1000ULL;
        sample.delta_involuntary_switches = event.involuntary_switches;
        sample.max_wait_us = event.max_wait_ns / 1000ULL;
        sample.avg_wait_us = event.avg_wait_ns / 1000ULL;
        sample.aggregate_count = event.aggregate_count;
        sample.valid = true;
        pending.push_back(sample);
        return pending.size() >= owner->config_.sched_ebpf_max_events_per_poll ? 1 : 0;
    }

    void clear_u32_map(int fd)
    {
        uint32_t key = 0;
        uint32_t next_key = 0;
        while (bpf_map_get_next_key(fd, nullptr, &next_key) == 0) {
            key = next_key;
            bpf_map_delete_elem(fd, &key);
        }
    }

    SchedDelayBpfCounters read_counters()
    {
        const uint32_t key = 0;
        SchedDelayBpfCounters counters{};
        if (bpf_map_lookup_elem(bpf_map__fd(skel->maps.counters), &key, &counters) != 0) {
            ++poll_failures;
        }
        {
            std::lock_guard<std::mutex> lock(runtime_stats_mutex_);
            runtime_stats.ebpf_ringbuf_drops = counters.ringbuf_drops;
            runtime_stats.ebpf_allowlist_exec_seen = counters.allowlist_exec_seen;
            runtime_stats.ebpf_allowlist_exit_cleaned = counters.allowlist_exit_cleaned;
            runtime_stats.ebpf_allowlist_stale_hits = counters.allowlist_stale_hits;
            runtime_stats.ebpf_aggregate_drops = counters.aggregate_drops;
        }
        return counters;
    }

    uint64_t read_counter_delta(const SchedDelayBpfCounters& counters)
    {
        if (!skel) {
            return 1;
        }
        const uint64_t current_total =
            counters.ringbuf_drops + counters.allowlist_stale_hits + counters.aggregate_drops;
        const uint64_t delta = current_total >= last_counter_total ? current_total - last_counter_total : 0;
        last_counter_total = current_total;
        return delta;
    }

    SchedDelayCollectorRuntimeStats runtime_stats_snapshot() const
    {
        std::lock_guard<std::mutex> lock(runtime_stats_mutex_);
        return runtime_stats;
    }

    EbpfSchedDelayCollector* owner{nullptr};
    sched_delay_bpf* skel{nullptr};
    ring_buffer* ring_buffer_{nullptr};
    std::vector<SchedDelaySample> pending;
    uint64_t poll_failures{0};
    uint64_t last_counter_total{0};
    uint64_t last_exec_seen{0};
    mutable std::mutex runtime_stats_mutex_;
    SchedDelayCollectorRuntimeStats runtime_stats{};
    std::chrono::steady_clock::time_point last_filter_refresh{};
};

EbpfSchedDelayCollector::EbpfSchedDelayCollector(const MonitorConfig& config)
    : config_(config),
      impl_(std::make_unique<Impl>(this)),
      fallback_(std::make_unique<SchedDelayCollector>(config))
{
    initialized_ = impl_->init();
    if (!initialized_) {
        last_failure_count_ = 1;
    }
}

EbpfSchedDelayCollector::~EbpfSchedDelayCollector() = default;

std::vector<SchedDelaySample> EbpfSchedDelayCollector::collect()
{
    last_failure_count_ = 0;
    if (!config_.sched_delay_enable) {
        return {};
    }
    if (!initialized_) {
        ++last_failure_count_;
        return fallback_->collect();
    }

    std::vector<SchedDelaySample> samples = impl_->poll();
    if (impl_->poll_failures > 0) {
        last_failure_count_ += impl_->poll_failures;
        impl_->poll_failures = 0;
    }
    return samples;
}

bool EbpfSchedDelayCollector::accepts(int32_t pid, int32_t tid) const
{
    (void)pid;
    (void)tid;
    return true;
}

SchedDelayCollectorRuntimeStats EbpfSchedDelayCollector::runtime_stats() const
{
    return impl_ ? impl_->runtime_stats_snapshot() : SchedDelayCollectorRuntimeStats{};
}

} // namespace lisysm
