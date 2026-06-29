#pragma once

#include "minilisysm/core/config.hpp"
#include "minilisysm/interfaces/event_sink.hpp"
#include "minilisysm/storage/event_serializer.hpp"

#include <atomic>
#include <filesystem>
#include <mutex>
#include <memory>
#include <string>
#include <thread>
#include <vector>

struct addrinfo;

namespace lisysm {

class NetworkEventSink : public EventSink {
  public:
    explicit NetworkEventSink(const MonitorConfig& config);
    ~NetworkEventSink() override;

    NetworkEventSink(const NetworkEventSink&) = delete;
    NetworkEventSink& operator=(const NetworkEventSink&) = delete;

    const char* name() const override {
        return "network";
    }
    SpscRingBuffer<InternalEvent>* add_input_queue(size_t capacity) override;
    bool start() override;
    void stop() override;
    SinkStats stats() const override;

  private:
    struct Endpoint {
        std::string host;
        uint16_t port{80};
        std::string path{"/events"};
        bool valid{false};
    };

    void run();
    void drain_queues_once();
    struct WalRecord {
        std::filesystem::path path;
        std::string json_line;
    };

    void append_wal(const std::string& json_line);
    void load_wal();
    void rewrite_wal_locked();
    void ack_pending(size_t acked_count);
    bool flush_pending();
    bool post_batch(const std::vector<std::string>& batch) const;
    bool connect_with_timeout(int fd, const addrinfo* target) const;
    Endpoint parse_endpoint() const;
    uint64_t wal_bytes() const;
    bool wal_over_limit() const;
    std::filesystem::path next_wal_path();
    void enforce_wal_limit();

    const MonitorConfig& config_;
    EventSerializer serializer_;
    std::vector<std::unique_ptr<SpscRingBuffer<InternalEvent>>> queues_;
    std::vector<WalRecord> pending_;
    mutable std::mutex pending_mutex_;
    std::filesystem::path current_wal_path_;
    uint64_t current_wal_size_{0};
    uint64_t current_wal_index_{1};
    Endpoint endpoint_;
    std::atomic<bool> running_{false};
    std::thread worker_;
    std::atomic<uint64_t> accepted_events_{0};
    std::atomic<uint64_t> sent_events_{0};
    std::atomic<uint64_t> send_errors_{0};
    std::atomic<uint64_t> retry_count_{0};
    std::atomic<uint64_t> wal_overflow_dropped_events_{0};
};

} // namespace lisysm
