#pragma once

#include "minilisysm/core/config.hpp"
#include "minilisysm/core/event.hpp"
#include "minilisysm/interfaces/event_sink.hpp"
#include "minilisysm/queue/spsc_ring_buffer.hpp"
#include "minilisysm/storage/event_serializer.hpp"

#include <atomic>
#include <cstdint>
#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace lisysm {

class JsonlEventSink : public EventSink {
  public:
    explicit JsonlEventSink(const MonitorConfig& config);
    ~JsonlEventSink() override;

    JsonlEventSink(const JsonlEventSink&) = delete;
    JsonlEventSink& operator=(const JsonlEventSink&) = delete;

    const char* name() const override {
        return "jsonl";
    }
    SpscRingBuffer<InternalEvent>* add_input_queue(size_t capacity) override;
    bool start() override;
    void stop() override;
    SinkStats stats() const override;

  private:
    void run();
    void drain_queues(std::string& line);
    bool write_event(const InternalEvent& event, std::string& line);
    void write_summary(const InternalEvent& event);
    std::string make_summary_line(const InternalEvent& event) const;
    bool open_next_file();
    void rotate_if_needed();
    void fsync_if_allowed(EventLevel level);
    void enforce_cache_limit();

    const MonitorConfig& config_;
    std::vector<std::unique_ptr<SpscRingBuffer<InternalEvent>>> queues_;
    EventSerializer serializer_;
    std::atomic<bool> running_{false};
    std::thread worker_;
    std::ofstream stream_;
    std::ofstream summary_stream_;
    std::string current_path_;
    std::string current_summary_path_;
    std::string file_prefix_;
    uint64_t current_size_{0};
    uint32_t file_index_{0};
    mutable std::atomic<uint64_t> written_events_{0};
    mutable std::atomic<uint64_t> write_errors_{0};
    mutable std::atomic<uint64_t> rotated_files_{0};
    mutable std::atomic<uint64_t> fsync_count_{0};
    mutable std::atomic<uint64_t> fsync_failures_{0};
    mutable std::atomic<uint64_t> fsync_rate_limited_{0};
    uint64_t fsync_window_start_ms_{0};
    uint32_t fsync_in_window_{0};
    bool current_file_needs_directory_sync_{false};
};

} // namespace lisysm
