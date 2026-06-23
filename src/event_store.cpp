#include "lisysm/event_store.hpp"
#include "lisysm/thread_policy.hpp"
#include "lisysm/time.hpp"

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

EventStore::EventStore(const MonitorConfig& config, SpscRingBuffer<InternalEvent>& queue)
    : config_(config), queue_(queue), serializer_(config)
{
}

EventStore::~EventStore()
{
    stop();
}

bool EventStore::start()
{
    if (!config_.persistence_enable) {
        return true;
    }
    running_.store(true);
    worker_ = std::thread(&EventStore::run, this);
    return true;
}

void EventStore::stop()
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

StoreStats EventStore::stats() const
{
    return StoreStats{
        written_events_.load(),
        write_errors_.load(),
        rotated_files_.load(),
        fsync_count_.load(),
    };
}

void EventStore::run()
{
    std::string ignored;
    set_current_thread_affinity(config_.persist_thread_cpu, &ignored);
    set_current_thread_nice(config_.background_nice, &ignored);

    if (!open_next_file()) {
        write_errors_.fetch_add(1);
    }

    while (running_.load() || queue_.depth() > 0) {
        InternalEvent event;
        if (!queue_.pop(event)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            continue;
        }
        if (!stream_.is_open() && !open_next_file()) {
            write_errors_.fetch_add(1);
            continue;
        }
        const std::string line = serializer_.to_json_line(event);
        stream_ << line;
        if (!stream_) {
            write_errors_.fetch_add(1);
            stream_.close();
            continue;
        }
        current_size_ += line.size();
        written_events_.fetch_add(1);
        fsync_if_allowed(event.level);
        rotate_if_needed();
        enforce_cache_limit();
    }
}

bool EventStore::open_next_file()
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

void EventStore::rotate_if_needed()
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

void EventStore::fsync_if_allowed(EventLevel level)
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

void EventStore::enforce_cache_limit()
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
