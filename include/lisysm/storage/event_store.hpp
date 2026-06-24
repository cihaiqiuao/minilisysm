#pragma once

#include "lisysm/core/config.hpp"
#include "lisysm/core/event.hpp"
#include "lisysm/storage/event_serializer.hpp"
#include "lisysm/queue/spsc_ring_buffer.hpp"

#include <atomic>
#include <cstdint>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

namespace lisysm {

struct StoreStats {
    uint64_t written_events{0};
    uint64_t write_errors{0};
    uint64_t rotated_files{0};
    uint64_t fsync_count{0};
};

class EventStore {
public:
    EventStore(const MonitorConfig& config, std::vector<SpscRingBuffer<InternalEvent>*>& queues);
    ~EventStore();

    EventStore(const EventStore&) = delete;
    EventStore& operator=(const EventStore&) = delete;

    bool start();
    void stop();
    StoreStats stats() const;

private:
    void run();
    bool open_next_file();
    void rotate_if_needed();
    void fsync_if_allowed(EventLevel level);
    void enforce_cache_limit();

    const MonitorConfig& config_;
    std::vector<SpscRingBuffer<InternalEvent>*>& queues_;
    EventSerializer serializer_;
    std::atomic<bool> running_{false};
    std::thread worker_;
    std::ofstream stream_;
    std::string current_path_;
    uint64_t current_size_{0};
    uint32_t file_index_{0};
    mutable std::atomic<uint64_t> written_events_{0};
    mutable std::atomic<uint64_t> write_errors_{0};
    mutable std::atomic<uint64_t> rotated_files_{0};
    mutable std::atomic<uint64_t> fsync_count_{0};
    uint64_t fsync_window_start_ms_{0};
    uint32_t fsync_in_window_{0};
};

} // namespace lisysm
