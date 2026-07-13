#include "minilisysm/storage/jsonl_event_sink.hpp"
#include "minilisysm/core/time.hpp"
#include "minilisysm/runtime/thread_policy.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <string_view>
#include <vector>

#if defined(__linux__)
#include <fcntl.h>
#include <unistd.h>
#endif

namespace lisysm {
namespace fs = std::filesystem;
namespace {

const char* enabled(bool value) {
    return value ? "enabled" : "disabled";
}

const char* color_for(EventLevel level) {
    switch (level) {
    case EventLevel::Critical:
        return "\033[1;31m";
    case EventLevel::Warning:
        return "\033[1;33m";
    case EventLevel::Recovery:
        return "\033[1;32m";
    case EventLevel::Info:
        return "\033[0;36m";
    }
    return "";
}

const char* event_type_cn(EventType type) {
    switch (type) {
    case EventType::MonitorStarted:
        return "监控启动";
    case EventType::MemoryPressure:
        return "系统内存压力";
    case EventType::MonitorOverrun:
        return "监控采集超时";
    case EventType::QueuePressure:
        return "监控队列压力";
    case EventType::StoragePressure:
        return "本地存储压力";
    case EventType::CollectorFailure:
        return "采集器失败";
    case EventType::MonitorMemoryPressure:
        return "监控自身内存压力";
    case EventType::SchedDelayRisk:
        return "调度延迟风险";
    case EventType::IoDelayRisk:
        return "I/O 延迟风险";
    case EventType::CpuUsageRisk:
        return "CPU 占用风险";
    }
    return "未知事件";
}

const char* evidence_key_cn(const char* key) {
    const std::string_view value(key);
    if (value == "recovery_threshold_mb")
        return "恢复阈值(MB)";
    if (value == "max_observed_rss_mb")
        return "最大观测 RSS(MB)";
    if (value == "max_observed_available_mb")
        return "最大观测可用内存(MB)";
    if (value == "source_queue_percent")
        return "源队列占用(%)";
    if (value == "sink_queue_percent")
        return "落地队列占用(%)";
    if (value == "total_dropped_count")
        return "总丢弃事件数";
    if (value == "dispatcher_failures")
        return "分发失败数";
    if (value == "critical_dropped_count")
        return "严重事件丢弃数";
    if (value == "high_watermark_percent")
        return "队列最高水位(%)";
    if (value == "delta_involuntary_switches")
        return "非自愿上下文切换增量";
    if (value == "recovery_wait_sum_us")
        return "恢复等待时间阈值(us)";
    if (value == "max_observed_wait_sum_us")
        return "最大观测等待时间(us)";
    if (value == "max_wait_us")
        return "最大单次等待(us)";
    if (value == "avg_wait_us")
        return "平均等待(us)";
    if (value == "aggregate_count")
        return "聚合样本数";
    if (value == "delta_io_count")
        return "I/O 完成数增量";
    if (value == "util_percent")
        return "设备忙碌率(%)";
    if (value == "in_flight")
        return "当前未完成 I/O 数";
    if (value == "recovery_await_ms")
        return "恢复 await 阈值(ms)";
    if (value == "max_observed_await_ms")
        return "最大观测 await(ms)";
    if (value == "recovery_percent")
        return "恢复阈值(%)";
    if (value == "max_observed_percent")
        return "最大观测占用率(%)";
    if (value == "delta_total_jiffies")
        return "CPU 总 jiffies 增量";
    if (value == "delta_idle_jiffies")
        return "CPU 空闲 jiffies 增量";
    if (value == "collector_id")
        return "采集器 ID";
    return key;
}

std::string make_file_prefix() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t seconds = std::chrono::system_clock::to_time_t(now);
    std::tm local_time{};
#if defined(_WIN32)
    localtime_s(&local_time, &seconds);
#else
    localtime_r(&seconds, &local_time);
#endif
    std::ostringstream output;
    output << "minilisysm-events-" << std::put_time(&local_time, "%Y%m%d-%H%M%S");
#if defined(__linux__)
    output << "-p" << static_cast<long>(::getpid());
#endif
    return output.str();
}

std::string realtime_string(uint64_t timestamp_ms) {
    const std::time_t seconds = static_cast<std::time_t>(timestamp_ms / 1000);
    std::tm local_time{};
#if defined(_WIN32)
    localtime_s(&local_time, &seconds);
#else
    localtime_r(&seconds, &local_time);
#endif
    std::ostringstream output;
    output << std::put_time(&local_time, "%Y-%m-%d %H:%M:%S") << '.' << std::setw(3) << std::setfill('0')
           << (timestamp_ms % 1000);
    return output.str();
}

} // namespace

JsonlEventSink::JsonlEventSink(const MonitorConfig& config)
    : config_(config), serializer_(config), file_prefix_(make_file_prefix()) {}

JsonlEventSink::~JsonlEventSink() {
    stop();
}

bool JsonlEventSink::start() {
    if (!config_.persistence_enable) {
        spdlog::info("jsonl event sink disabled: persistence_enable=false");
        return true;
    }
    running_.store(true);
    worker_ = std::thread(&JsonlEventSink::run, this);
    spdlog::info("jsonl event sink started: cache_path={} summary={}", config_.cache_path,
                 enabled(config_.summary_enable));
    return true;
}

SpscRingBuffer<InternalEvent>* JsonlEventSink::add_input_queue(size_t capacity) {
    queues_.push_back(std::make_unique<SpscRingBuffer<InternalEvent>>(
        capacity, config_.critical_reserved_slots, config_.drop_info_when_full, config_.drop_warning_when_full));
    return queues_.back().get();
}

void JsonlEventSink::stop() {
    const bool was_running = running_.exchange(false);
    if (worker_.joinable()) {
        worker_.join();
    }
    if (stream_.is_open()) {
        stream_.flush();
        stream_.close();
    }
    if (summary_stream_.is_open()) {
        summary_stream_.flush();
        summary_stream_.close();
    }
    if (was_running) {
        spdlog::info("jsonl event sink stopped: written_events={} write_errors={} rotated_files={}",
                     written_events_.load(), write_errors_.load(), rotated_files_.load());
    }
}

SinkStats JsonlEventSink::stats() const {
    uint64_t dropped_events = 0;
    uint64_t dropped_critical_events = 0;
    uint64_t reserve_reject_events = 0;
    uint64_t pending_events = 0;
    size_t queue_capacity = 0;
    size_t queue_high_watermark = 0;
    for (const std::unique_ptr<SpscRingBuffer<InternalEvent>>& queue : queues_) {
        if (!queue) {
            continue;
        }
        const QueueStats queue_stats = queue->stats();
        dropped_events += queue_stats.push_fail_count;
        dropped_critical_events += queue_stats.dropped_critical_count;
        reserve_reject_events += queue_stats.reserve_reject_count;
        pending_events += queue->depth();
        queue_capacity += queue->usable_capacity();
        queue_high_watermark = std::max(queue_high_watermark, queue_stats.high_watermark);
    }
    return SinkStats{
        written_events_.load() + pending_events,
        dropped_events,
        dropped_critical_events,
        reserve_reject_events,
        written_events_.load(),
        write_errors_.load(),
        rotated_files_.load(),
        fsync_count_.load(),
        0,
        0,
        0,
        0,
        0,
        0,
        pending_events,
        queue_capacity,
        queue_high_watermark,
    };
}

void JsonlEventSink::run() {
    std::string ignored;
    if (!set_current_thread_affinity(config_.persist_thread_cpu, &ignored) && config_.persist_thread_cpu >= 0) {
        spdlog::warn("failed to set jsonl sink CPU affinity: cpu={} reason={}", config_.persist_thread_cpu, ignored);
    }
    if (!set_current_thread_nice(config_.background_nice, &ignored)) {
        spdlog::warn("failed to set jsonl sink nice: nice={} reason={}", config_.background_nice, ignored);
    }

    if (!open_next_file()) {
        write_errors_.fetch_add(1);
        spdlog::error("failed to open initial jsonl event file: cache_path={}", config_.cache_path);
    }

    std::string line;
    line.reserve(1024);
    size_t queue_index = 0;
    while (running_.load()) {
        InternalEvent event;
        bool consumed = false;
        for (size_t i = 0; i < queues_.size(); ++i) {
            queue_index = (queue_index + 1) % queues_.size();
            if (queues_[queue_index] && queues_[queue_index]->pop(event)) {
                consumed = true;
                break;
            }
        }
        if (!consumed) {
            std::this_thread::sleep_for(std::chrono::milliseconds(config_.sink_idle_sleep_ms));
            continue;
        }
        write_event(event, line);
    }
    drain_queues(line);
}

void JsonlEventSink::drain_queues(std::string& line) {
    bool drained = false;
    do {
        drained = false;
        for (const std::unique_ptr<SpscRingBuffer<InternalEvent>>& queue : queues_) {
            InternalEvent event;
            while (queue && queue->pop(event)) {
                drained = true;
                write_event(event, line);
            }
        }
    } while (drained);
}

bool JsonlEventSink::write_event(const InternalEvent& event, std::string& line) {
    if (!stream_.is_open() && !open_next_file()) {
        write_errors_.fetch_add(1);
        spdlog::error("failed to open jsonl event file while writing: cache_path={}", config_.cache_path);
        return false;
    }
    serializer_.to_json_line(event, line);
    stream_ << line;
    if (!stream_) {
        write_errors_.fetch_add(1);
        spdlog::error("failed to write jsonl event file: path={}", current_path_);
        stream_.close();
        return false;
    }
    current_size_ += line.size();
    write_summary(event);
    written_events_.fetch_add(1);
    fsync_if_allowed(event.level);
    rotate_if_needed();
    enforce_cache_limit();
    return true;
}

bool JsonlEventSink::open_next_file() {
    const fs::path jsonl_dir = fs::path(config_.cache_path) / "jsonl";
    const fs::path summary_dir = fs::path(config_.cache_path) / "summary";
    fs::create_directories(jsonl_dir);
    if (config_.summary_enable) {
        fs::create_directories(summary_dir);
    }
    ++file_index_;
    std::ostringstream name;
    name << file_prefix_ << "-part" << std::setw(6) << std::setfill('0') << file_index_ << ".jsonl";
    current_path_ = (jsonl_dir / name.str()).string();
    stream_.open(current_path_, std::ios::out | std::ios::app | std::ios::binary);
    current_size_ = stream_.is_open() && fs::exists(current_path_) ? fs::file_size(current_path_) : 0;

    if (config_.summary_enable) {
        std::ostringstream summary_name;
        summary_name << file_prefix_ << "-part" << std::setw(6) << std::setfill('0') << file_index_ << ".summary.log";
        current_summary_path_ = (summary_dir / summary_name.str()).string();
        summary_stream_.open(current_summary_path_, std::ios::out | std::ios::app | std::ios::binary);
        if (!summary_stream_) {
            write_errors_.fetch_add(1);
            spdlog::warn("failed to open summary event file: path={}", current_summary_path_);
            summary_stream_.close();
        }
    }
    if (stream_.is_open()) {
        spdlog::info("opened jsonl event file: path={} summary_path={}", current_path_, current_summary_path_);
    } else {
        spdlog::error("failed to open jsonl event file: path={}", current_path_);
    }
    return stream_.is_open();
}

void JsonlEventSink::write_summary(const InternalEvent& event) {
    if (!config_.summary_enable) {
        return;
    }
    if (!summary_stream_.is_open()) {
        return;
    }
    summary_stream_ << make_summary_line(event);
    if (!summary_stream_) {
        write_errors_.fetch_add(1);
        spdlog::warn("failed to write summary event file: path={}", current_summary_path_);
        summary_stream_.close();
    }
}

std::string JsonlEventSink::make_summary_line(const InternalEvent& event) const {
    std::ostringstream output;
    if (config_.summary_color) {
        output << color_for(event.level);
    }
    output << realtime_string(event.realtime_ms) << " [" << to_string(event.level) << "] "
           << event_type_cn(event.event_type) << " (" << to_string(event.event_type) << ")"
           << " status=" << to_string(event.status) << " 事件ID=" << config_.device_id << '-' << event.sequence;
    if (config_.summary_color) {
        output << "\033[0m";
    }
    output << '\n';

    output << "  当前值(value)=" << std::fixed << std::setprecision(3) << event.value;
    if (event.warning_threshold != 0.0) {
        output << " 警告阈值(warn)=" << event.warning_threshold;
    }
    if (event.critical_threshold != 0.0) {
        output << " 严重阈值(crit)=" << event.critical_threshold;
    }
    if (event.window_sec != 0) {
        output << " 统计窗口(window)=" << event.window_sec << "秒";
    }
    if (event.continuous_hit_count != 0) {
        output << " 连续命中(hits)=" << event.continuous_hit_count << "次";
    }
    output << '\n';

    if (event.target[0] != '\0') {
        output << "  目标=" << event.target.data();
    }
    if (event.pid >= 0) {
        output << " 进程PID=" << event.pid;
    }
    if (event.tid >= 0) {
        output << " 线程TID=" << event.tid;
    }
    if (event.target[0] != '\0' || event.pid >= 0 || event.tid >= 0) {
        output << '\n';
    }
    if (event.evidence_count > 0) {
        output << "  证据: ";
        for (uint32_t i = 0; i < event.evidence_count && i < event.evidence.size(); ++i) {
            if (i != 0) {
                output << ", ";
            }
            output << evidence_key_cn(event.evidence[i].key.data()) << '(' << event.evidence[i].key.data()
                   << ")=" << event.evidence[i].value;
        }
        output << '\n';
    }
    output << '\n';
    return output.str();
}

void JsonlEventSink::rotate_if_needed() {
    const uint64_t max_bytes = config_.file_rotate_mb * 1024ULL * 1024ULL;
    if (current_size_ < max_bytes) {
        return;
    }
    stream_.flush();
    stream_.close();
    if (summary_stream_.is_open()) {
        summary_stream_.flush();
        summary_stream_.close();
    }
    rotated_files_.fetch_add(1);
    spdlog::info("rotating jsonl event file: path={} size_bytes={} max_bytes={}", current_path_, current_size_,
                 max_bytes);
    open_next_file();
}

void JsonlEventSink::fsync_if_allowed(EventLevel level) {
    if (!config_.critical_fsync || level != EventLevel::Critical) {
        return;
    }
    const uint64_t now = monotonic_ms();
    if (now - fsync_window_start_ms_ >= 60000) {
        fsync_window_start_ms_ = now;
        fsync_in_window_ = 0;
    }
    if (fsync_in_window_ >= config_.max_fsync_per_minute) {
        return;
    }
    stream_.flush();
#if defined(__linux__)
    int fd = ::open(current_path_.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd >= 0) {
        if (::fdatasync(fd) != 0) {
            spdlog::warn("fdatasync failed for jsonl event file: path={}", current_path_);
        }
        ::close(fd);
    } else {
        spdlog::warn("failed to open jsonl event file for fdatasync: path={}", current_path_);
    }
#endif
    ++fsync_in_window_;
    fsync_count_.fetch_add(1);
}

void JsonlEventSink::enforce_cache_limit() {
    const uint64_t max_bytes = config_.cache_max_mb * 1024ULL * 1024ULL;
    uint64_t total = 0;
    std::vector<fs::directory_entry> files;
    std::error_code ec;
    for (const auto& entry : fs::recursive_directory_iterator(config_.cache_path, ec)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        total += entry.file_size(ec);
        files.push_back(entry);
    }
    if (total <= max_bytes || files.empty()) {
        return;
    }
    std::sort(files.begin(), files.end(),
              [](const auto& a, const auto& b) { return a.last_write_time() < b.last_write_time(); });
    for (const auto& entry : files) {
        const std::string path = entry.path().string();
        if (path == current_path_ || path == current_summary_path_) {
            continue;
        }
        const uint64_t size = entry.file_size(ec);
        fs::remove(entry.path(), ec);
        if (ec) {
            spdlog::warn("failed to remove old event cache file: path={} error={}", path, ec.message());
        } else {
            spdlog::info("removed old event cache file: path={} size_bytes={} total_after_bytes={}", path, size,
                         total > size ? total - size : 0);
        }
        total = total > size ? total - size : 0;
        if (total <= max_bytes) {
            break;
        }
    }
}

} // namespace lisysm
