#include "minilisysm/collectors/sched_delay_collector.hpp"

#include <charconv>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string_view>
#include <system_error>

#if defined(__linux__)
#include <unistd.h>
#endif

namespace lisysm {
namespace fs = std::filesystem;
namespace {

bool parse_i32(std::string_view text, int32_t* value) {
    int32_t parsed = 0;
    if (std::from_chars(text.data(), text.data() + text.size(), parsed).ec != std::errc{}) {
        return false;
    }
    *value = parsed;
    return true;
}

std::string trim_newline(std::string value) {
    while (!value.empty() && (value.back() == '\n' || value.back() == '\r')) {
        value.pop_back();
    }
    return value;
}

bool parse_sched_value(std::string_view line, const char* key, double* value) {
    const std::string_view key_view(key);
    if (line.find(key_view) == std::string_view::npos) {
        return false;
    }
    const size_t colon = line.find(':');
    if (colon == std::string_view::npos) {
        return false;
    }
    std::string text(line.substr(colon + 1));
    std::istringstream input(text);
    double parsed = 0.0;
    input >> parsed;
    if (!input) {
        return false;
    }
    *value = parsed;
    return true;
}

uint64_t key_for(int32_t pid, int32_t tid) {
    return (static_cast<uint64_t>(static_cast<uint32_t>(pid)) << 32U) | static_cast<uint32_t>(tid);
}

} // namespace

SchedDelayCollector::SchedDelayCollector(const MonitorConfig& config, std::string proc_dir, Clock clock)
    : config_(config), proc_dir_(std::move(proc_dir)), clock_(clock != nullptr ? clock : monotonic_ms) {}

std::vector<SchedDelaySample> SchedDelayCollector::collect() {
    const auto collect_start = std::chrono::steady_clock::now();
    std::vector<SchedDelaySample> samples;
    const uint64_t now_ms = clock_();
    prune_expired_state(now_ms);
    last_failure_count_ = 0;
    if (!config_.sched_delay_enable) {
        return samples;
    }

    std::error_code proc_ec;
    const fs::directory_iterator proc_iter(proc_dir_, proc_ec);
    if (proc_ec) {
        ++last_failure_count_;
        return samples;
    }
    uint32_t scanned_threads = 0;
    for (const auto& proc : proc_iter) {
        if (!proc.is_directory()) {
            continue;
        }
        int32_t pid = -1;
        const std::string pid_text = proc.path().filename().string();
        if (!parse_i32(pid_text, &pid)) {
            continue;
        }
        if (!should_consider_process_id(pid)) {
            continue;
        }
        std::string process_comm;
        if (!cached_comm(static_cast<uint64_t>(static_cast<uint32_t>(pid)), (proc.path() / "comm").string(),
                         process_comm_cache_, &process_comm)) {
            continue;
        }
        if (!should_scan_process(pid, process_comm)) {
            continue;
        }

        const fs::path task_dir = proc.path() / "task";
        std::error_code task_ec;
        const fs::directory_iterator task_iter(task_dir, task_ec);
        if (task_ec) {
            ++last_failure_count_;
            continue;
        }
        for (const auto& task : task_iter) {
            if (samples.size() >= config_.sched_max_targets) {
                const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - collect_start);
                if (elapsed.count() > config_.sched_collector_overrun_warning_ms) {
                    ++last_failure_count_;
                }
                return samples;
            }
            if (scanned_threads >= config_.sched_proc_max_scan_threads) {
                ++last_failure_count_;
                const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - collect_start);
                if (elapsed.count() > config_.sched_collector_overrun_warning_ms) {
                    ++last_failure_count_;
                }
                return samples;
            }
            int32_t tid = -1;
            const std::string tid_text = task.path().filename().string();
            if (!parse_i32(tid_text, &tid)) {
                continue;
            }
            ++scanned_threads;
            if (!config_.sched_thread_whitelist.empty()) {
                std::string thread_comm;
                if (!cached_comm(key_for(pid, tid), (task.path() / "comm").string(), thread_comm_cache_,
                                 &thread_comm)) {
                    continue;
                }
                if (!should_scan_thread(thread_comm)) {
                    continue;
                }
            }

            uint64_t wait_sum_us = 0;
            uint64_t involuntary_switches = 0;
            if (!read_sched(pid, tid, &wait_sum_us, &involuntary_switches)) {
                continue;
            }
            const uint64_t key = key_for(pid, tid);
            const auto it = baselines_.find(key);
            if (it == baselines_.end() || wait_sum_us < it->second.wait_sum_us ||
                involuntary_switches < it->second.involuntary_switches) {
                baselines_[key] = Baseline{wait_sum_us, involuntary_switches, now_ms};
                continue;
            }

            const uint64_t prev_wait_sum = it->second.wait_sum_us;
            const uint64_t prev_switches = it->second.involuntary_switches;
            baselines_[key] = Baseline{wait_sum_us, involuntary_switches, now_ms};

            SchedDelaySample sample;
            sample.pid = pid;
            sample.tid = tid;
            sample.delta_wait_sum_us = wait_sum_us - prev_wait_sum;
            sample.delta_involuntary_switches = involuntary_switches - prev_switches;
            sample.max_wait_us = sample.delta_wait_sum_us;
            sample.avg_wait_us = sample.delta_involuntary_switches == 0
                                     ? sample.delta_wait_sum_us
                                     : sample.delta_wait_sum_us / sample.delta_involuntary_switches;
            sample.aggregate_count = sample.delta_involuntary_switches;
            sample.valid = true;
            samples.push_back(sample);
        }
    }
    const auto elapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - collect_start);
    if (elapsed.count() > config_.sched_collector_overrun_warning_ms) {
        ++last_failure_count_;
    }
    return samples;
}

bool SchedDelayCollector::should_consider_process_id(int32_t pid) const {
    if (!config_.sched_process_whitelist.empty()) {
        return true;
    }
#if defined(__linux__)
    return pid == static_cast<int32_t>(::getpid());
#else
    return false;
#endif
}

bool SchedDelayCollector::should_scan_process(int32_t pid, const std::string& comm) const {
    if (config_.sched_process_whitelist.empty()) {
#if defined(__linux__)
        return pid == static_cast<int32_t>(::getpid());
#else
        return false;
#endif
    }
    return contains_name(config_.sched_process_whitelist, comm);
}

bool SchedDelayCollector::should_scan_thread(const std::string& comm) const {
    return config_.sched_thread_whitelist.empty() || contains_name(config_.sched_thread_whitelist, comm);
}

bool SchedDelayCollector::cached_comm(uint64_t key, const std::string& path,
                                      std::unordered_map<uint64_t, CommCacheEntry>& cache, std::string* comm) {
    const auto now = std::chrono::steady_clock::now();
    const auto refresh = std::chrono::milliseconds(config_.sched_proc_cache_refresh_ms);
    const auto it = cache.find(key);
    if (it != cache.end() && now - it->second.refreshed_at < refresh) {
        it->second.last_seen_ms = clock_();
        *comm = it->second.comm;
        return true;
    }
    std::string refreshed;
    if (!read_comm(path, &refreshed)) {
        cache.erase(key);
        return false;
    }
    cache[key] = CommCacheEntry{refreshed, now, clock_()};
    *comm = std::move(refreshed);
    return true;
}

void SchedDelayCollector::prune_expired_state(uint64_t now_ms) {
    const uint64_t ttl_sec = config_.state_ttl_sec == 0 ? 3600 : config_.state_ttl_sec;
    const uint64_t ttl_ms = ttl_sec * 1000ULL;
    const auto erase_expired = [now_ms, ttl_ms](auto& map) {
        for (auto it = map.begin(); it != map.end();) {
            if (it->second.last_seen_ms != 0 && now_ms - it->second.last_seen_ms >= ttl_ms) {
                it = map.erase(it);
            } else {
                ++it;
            }
        }
    };
    erase_expired(baselines_);
    erase_expired(process_comm_cache_);
    erase_expired(thread_comm_cache_);
}

bool SchedDelayCollector::read_comm(const std::string& path, std::string* comm) const {
    std::ifstream input(path);
    if (!input) {
        return false;
    }
    std::string line;
    std::getline(input, line);
    *comm = trim_newline(line);
    return !comm->empty();
}

bool SchedDelayCollector::read_sched(int32_t pid, int32_t tid, uint64_t* wait_sum_us,
                                     uint64_t* involuntary_switches) const {
    std::ifstream input(proc_dir_ + "/" + std::to_string(pid) + "/task/" + std::to_string(tid) + "/sched");
    if (!input) {
        return false;
    }
    std::string line;
    bool found_wait = false;
    bool found_switches = false;
    while (std::getline(input, line)) {
        double parsed = 0.0;
        if (parse_sched_value(line, "se.statistics.wait_sum", &parsed)) {
            *wait_sum_us = static_cast<uint64_t>(parsed * 1000.0);
            found_wait = true;
            continue;
        }
        if (parse_sched_value(line, "nr_involuntary_switches", &parsed)) {
            *involuntary_switches = static_cast<uint64_t>(parsed);
            found_switches = true;
            continue;
        }
    }
    return found_wait && found_switches;
}

bool SchedDelayCollector::contains_name(const std::vector<std::string>& names, const std::string& value) const {
    for (const std::string& name : names) {
        if (name == value) {
            return true;
        }
    }
    return false;
}

} // namespace lisysm
