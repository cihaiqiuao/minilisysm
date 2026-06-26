#include "minilisysm/storage/jsonl_event_sink.hpp"
#include "minilisysm/core/time.hpp"
#include "minilisysm/runtime/thread_policy.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <vector>

#if defined(__linux__)
#include <fcntl.h>
#include <unistd.h>
#endif

namespace lisysm {
namespace fs = std::filesystem;

JsonlEventSink::JsonlEventSink(const MonitorConfig& config)
    : config_(config),
      serializer_(config)
{
}

JsonlEventSink::~JsonlEventSink()
{
    stop();
}

bool JsonlEventSink::start()
{
    if (!config_.persistence_enable) {
        return true;
    }
    running_.store(true);
    worker_ = std::thread(&JsonlEventSink::run, this);
    return true;
}

SpscRingBuffer<InternalEvent>* JsonlEventSink::add_input_queue(size_t capacity)
{
    queues_.push_back(std::make_unique<SpscRingBuffer<InternalEvent>>(
        capacity,
        config_.critical_reserved_slots,
        config_.drop_info_when_full,
        config_.drop_warning_when_full));
    return queues_.back().get();
}

void JsonlEventSink::stop()
{
    running_.store(false);
    if (worker_.joinable()) {
        worker_.join();
    }
    if (stream_.is_open()) {
        stream_.flush();
        stream_.close();
    }
}

SinkStats JsonlEventSink::stats() const
{
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

void JsonlEventSink::run()
{
    std::string ignored;
    set_current_thread_affinity(config_.persist_thread_cpu, &ignored);
    set_current_thread_nice(config_.background_nice, &ignored);

    if (!open_next_file()) {
        write_errors_.fetch_add(1);
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

void JsonlEventSink::drain_queues(std::string& line)
{
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

bool JsonlEventSink::write_event(const InternalEvent& event, std::string& line)
{
    if (!stream_.is_open() && !open_next_file()) {
        write_errors_.fetch_add(1);
        return false;
    }
    serializer_.to_json_line(event, line);
    stream_ << line;
    if (!stream_) {
        write_errors_.fetch_add(1);
        stream_.close();
        return false;
    }
    current_size_ += line.size();
    written_events_.fetch_add(1);
    fsync_if_allowed(event.level);
    rotate_if_needed();
    enforce_cache_limit();
    return true;
}

bool JsonlEventSink::open_next_file()
{
    fs::create_directories(config_.cache_path);
    ++file_index_;
    std::ostringstream name;
    name << "events_" << std::setw(6) << std::setfill('0') << file_index_ << ".jsonl";
    current_path_ = (fs::path(config_.cache_path) / name.str()).string();
    stream_.open(current_path_, std::ios::out | std::ios::app | std::ios::binary);
    current_size_ = stream_.is_open() && fs::exists(current_path_) ? fs::file_size(current_path_) : 0;
    return stream_.is_open();
}

void JsonlEventSink::rotate_if_needed()
{
    const uint64_t max_bytes = config_.file_rotate_mb * 1024ULL * 1024ULL;
    if (current_size_ < max_bytes) {
        return;
    }
    stream_.flush();
    stream_.close();
    rotated_files_.fetch_add(1);
    open_next_file();
}

void JsonlEventSink::fsync_if_allowed(EventLevel level)
{
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
        ::fdatasync(fd);
        ::close(fd);
    }
#endif
    ++fsync_in_window_;
    fsync_count_.fetch_add(1);
}

void JsonlEventSink::enforce_cache_limit()
{
    const uint64_t max_bytes = config_.cache_max_mb * 1024ULL * 1024ULL;
    uint64_t total = 0;
    std::vector<fs::directory_entry> files;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(config_.cache_path, ec)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        total += entry.file_size(ec);
        files.push_back(entry);
    }
    if (total <= max_bytes || files.empty()) {
        return;
    }
    std::sort(files.begin(), files.end(), [](const auto& a, const auto& b) {
        return a.last_write_time() < b.last_write_time();
    });
    for (const auto& entry : files) {
        if (entry.path().string() == current_path_) {
            continue;
        }
        const uint64_t size = entry.file_size(ec);
        fs::remove(entry.path(), ec);
        total = total > size ? total - size : 0;
        if (total <= max_bytes) {
            break;
        }
    }
}

} // namespace lisysm
