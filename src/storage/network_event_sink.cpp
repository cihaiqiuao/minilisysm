#include "minilisysm/storage/network_event_sink.hpp"
#include "minilisysm/runtime/thread_policy.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

#if defined(__linux__)
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace lisysm {
namespace fs = std::filesystem;
namespace {

bool sync_file(const fs::path& path) {
#if defined(__linux__)
    const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return false;
    }
    const bool synced = ::fdatasync(fd) == 0;
    ::close(fd);
    return synced;
#else
    (void)path;
    return false;
#endif
}

bool sync_directory(const fs::path& path) {
#if defined(__linux__)
    const int fd = ::open(path.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd < 0) {
        return false;
    }
    const bool synced = ::fsync(fd) == 0;
    ::close(fd);
    return synced;
#else
    (void)path;
    return false;
#endif
}

fs::path wal_path_for_index(const fs::path& directory, uint64_t index) {
    std::ostringstream name;
    name << "events_" << std::setw(6) << std::setfill('0') << index << ".wal";
    return directory / name.str();
}

} // namespace

NetworkEventSink::NetworkEventSink(const MonitorConfig& config)
    : config_(config), serializer_(config), endpoint_(parse_endpoint()) {}

NetworkEventSink::~NetworkEventSink() {
    stop();
}

SpscRingBuffer<InternalEvent>* NetworkEventSink::add_input_queue(size_t capacity) {
    queues_.push_back(std::make_unique<SpscRingBuffer<InternalEvent>>(
        capacity, config_.critical_reserved_slots, config_.drop_info_when_full, config_.drop_warning_when_full));
    return queues_.back().get();
}

bool NetworkEventSink::start() {
    if (!config_.network_sink_enable) {
        spdlog::info("network event sink disabled");
        return true;
    }
    if (!endpoint_.valid) {
        spdlog::error("network event sink endpoint is invalid: endpoint={}", config_.network_endpoint);
        return false;
    }
    fs::create_directories(config_.network_wal_path);
    load_wal();
    if (current_wal_path_.empty()) {
        current_wal_path_ = next_wal_path();
    }
    running_.store(true);
    worker_ = std::thread(&NetworkEventSink::run, this);
    spdlog::info("network event sink started: endpoint={} wal_path={} pending_events={}", config_.network_endpoint,
                 config_.network_wal_path, pending_.size());
    return true;
}

void NetworkEventSink::stop() {
    const bool was_running = running_.exchange(false);
    if (worker_.joinable()) {
        worker_.join();
    }
    if (was_running) {
        spdlog::info("network event sink stopped: accepted={} sent={} send_errors={} retries={} wal_pending={}",
                     accepted_events_.load(), sent_events_.load(), send_errors_.load(), retry_count_.load(),
                     stats().wal_pending_events);
    }
}

SinkStats NetworkEventSink::stats() const {
    uint64_t dropped_events = 0;
    uint64_t dropped_critical_events = 0;
    uint64_t reserve_reject_events = 0;
    size_t queue_depth = 0;
    size_t queue_capacity = 0;
    size_t queue_high_watermark = 0;
    for (const auto& queue : queues_) {
        if (!queue) {
            continue;
        }
        const QueueStats queue_stats = queue->stats();
        dropped_events += queue_stats.push_fail_count;
        dropped_critical_events += queue_stats.dropped_critical_count;
        reserve_reject_events += queue_stats.reserve_reject_count;
        queue_depth += queue->depth();
        queue_capacity += queue->usable_capacity();
        queue_high_watermark = std::max(queue_high_watermark, queue_stats.high_watermark);
    }
    std::error_code ec;
    (void)ec;
    SinkStats stats;
    stats.accepted_events = accepted_events_.load() + queue_depth;
    stats.dropped_events = dropped_events;
    stats.dropped_critical_events = dropped_critical_events;
    stats.reserve_reject_events = reserve_reject_events;
    stats.write_errors = write_errors_.load();
    stats.sent_events = sent_events_.load();
    stats.send_errors = send_errors_.load();
    stats.retry_count = retry_count_.load();
    {
        std::lock_guard<std::mutex> lock(pending_mutex_);
        stats.wal_pending_events = pending_.size();
    }
    stats.wal_bytes = wal_bytes();
    stats.wal_overflow_dropped_events = wal_overflow_dropped_events_.load();
    stats.queue_depth = queue_depth;
    stats.queue_capacity = queue_capacity;
    stats.queue_high_watermark = queue_high_watermark;
    return stats;
}

void NetworkEventSink::run() {
    std::string ignored;
    if (!set_current_thread_affinity(config_.persist_thread_cpu, &ignored) && config_.persist_thread_cpu >= 0) {
        spdlog::warn("failed to set network sink CPU affinity: cpu={} reason={}", config_.persist_thread_cpu, ignored);
    }
    if (!set_current_thread_nice(config_.background_nice, &ignored)) {
        spdlog::warn("failed to set network sink nice: nice={} reason={}", config_.background_nice, ignored);
    }

    uint32_t retry_ms = config_.network_retry_base_ms;
    auto next_flush = std::chrono::steady_clock::now();
    while (running_.load()) {
        drain_queues_once();
        const auto now = std::chrono::steady_clock::now();
        bool has_pending = false;
        {
            std::lock_guard<std::mutex> lock(pending_mutex_);
            has_pending = !pending_.empty();
        }
        if (has_pending && now >= next_flush) {
            if (flush_pending()) {
                retry_ms = config_.network_retry_base_ms;
                next_flush = now + std::chrono::milliseconds(config_.network_flush_interval_ms);
            } else {
                retry_count_.fetch_add(1);
                spdlog::warn("network sink flush failed: retry_delay_ms={} endpoint={}", retry_ms,
                             config_.network_endpoint);
                next_flush = now + std::chrono::milliseconds(retry_ms);
                retry_ms = std::min<uint32_t>(retry_ms * 2, config_.network_retry_max_ms);
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(config_.sink_idle_sleep_ms));
    }
    drain_queues_once();
    while (true) {
        bool has_pending = false;
        {
            std::lock_guard<std::mutex> lock(pending_mutex_);
            has_pending = !pending_.empty();
        }
        if (!has_pending || !flush_pending()) {
            break;
        }
    }
}

void NetworkEventSink::drain_queues_once() {
    std::string line;
    line.reserve(1024);
    for (const auto& queue : queues_) {
        InternalEvent event;
        while (queue && queue->pop(event)) {
            if (wal_over_limit() && event.level != EventLevel::Critical) {
                const uint64_t dropped = wal_overflow_dropped_events_.fetch_add(1) + 1;
                if (dropped == 1 || dropped % 1000 == 0) {
                    spdlog::warn(
                        "network WAL over limit, dropping non-critical event: dropped={} wal_bytes={} max_mb={}",
                        dropped, wal_bytes(), config_.network_wal_max_mb);
                }
                continue;
            }
            serializer_.to_json_line(event, line);
            if (!append_wal(line)) {
                write_errors_.fetch_add(1);
                continue;
            }
            {
                std::lock_guard<std::mutex> lock(pending_mutex_);
                pending_.push_back(WalRecord{current_wal_path_, line});
            }
            accepted_events_.fetch_add(1);
        }
    }
    enforce_wal_limit();
}

bool NetworkEventSink::append_wal(const std::string& json_line) {
    fs::create_directories(config_.network_wal_path);
    if (current_wal_path_.empty()) {
        current_wal_path_ = next_wal_path();
    }
    const uint64_t segment_bytes = config_.network_wal_segment_mb * 1024ULL * 1024ULL;
    if (current_wal_size_ >= segment_bytes) {
        spdlog::info("rotating network WAL segment: path={} size_bytes={} max_bytes={}", current_wal_path_.string(),
                     current_wal_size_, segment_bytes);
        current_wal_path_ = next_wal_path();
        current_wal_size_ = 0;
    }
    std::ofstream stream(current_wal_path_, std::ios::out | std::ios::app | std::ios::binary);
    if (!stream) {
        spdlog::error("failed to open network WAL for append: path={}", current_wal_path_.string());
        return false;
    }
    stream << "0\t" << json_line;
    stream.flush();
    stream.close();
    if (!stream) {
        spdlog::error("failed to append network WAL: path={}", current_wal_path_.string());
        return false;
    }
    current_wal_size_ += json_line.size() + 2;
    return true;
}

void NetworkEventSink::load_wal() {
    std::lock_guard<std::mutex> lock(pending_mutex_);
    pending_.clear();
    fs::create_directories(config_.network_wal_path);
    std::vector<fs::path> segments;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(config_.network_wal_path, ec)) {
        if (entry.is_regular_file() && entry.path().extension() == ".wal") {
            segments.push_back(entry.path());
        }
    }
    std::sort(segments.begin(), segments.end());
    uint64_t max_index = 0;
    for (const fs::path& segment : segments) {
        const std::string stem = segment.stem().string();
        const size_t underscore = stem.find_last_of('_');
        if (underscore != std::string::npos) {
            try {
                max_index = std::max<uint64_t>(max_index, std::stoull(stem.substr(underscore + 1)));
            } catch (...) {
            }
        }
        std::ifstream stream(segment, std::ios::in | std::ios::binary);
        std::string line;
        while (std::getline(stream, line)) {
            if (line.size() > 2 && line[0] == '0' && line[1] == '\t') {
                pending_.push_back(WalRecord{segment, line.substr(2) + "\n"});
            }
        }
    }
    current_wal_index_ = max_index + 1;
    current_wal_path_ = next_wal_path();
    current_wal_size_ = 0;
    spdlog::info("network WAL loaded: path={} segments={} pending_events={}", config_.network_wal_path, segments.size(),
                 pending_.size());
}

bool NetworkEventSink::rewrite_wal_locked(size_t acked_count) {
    // Keep the old generation and in-memory queue intact until every replacement segment is durably published.
    acked_count = std::min(acked_count, pending_.size());
    std::vector<WalRecord> remaining(pending_.begin() + static_cast<std::ptrdiff_t>(acked_count), pending_.end());

    const fs::path wal_directory(config_.network_wal_path);
    std::vector<fs::path> old_segments;
    std::error_code ec;
    uint64_t next_index = current_wal_index_;
    for (const auto& entry : fs::directory_iterator(wal_directory, ec)) {
        if (entry.is_regular_file() && entry.path().extension() == ".wal") {
            old_segments.push_back(entry.path());
            const std::string stem = entry.path().stem().string();
            const size_t underscore = stem.find_last_of('_');
            if (underscore != std::string::npos) {
                try {
                    next_index =
                        std::max<uint64_t>(next_index, std::stoull(stem.substr(underscore + 1)) + 1);
                } catch (...) {
                }
            }
        }
    }

    struct StagedSegment {
        fs::path temporary_path;
        fs::path final_path;
        uint64_t size{0};
    };
    std::vector<StagedSegment> staged_segments;
    const uint64_t segment_bytes = config_.network_wal_segment_mb * 1024ULL * 1024ULL;
    size_t record_index = 0;
    do {
        StagedSegment segment;
        segment.final_path = wal_path_for_index(wal_directory, next_index++);
        segment.temporary_path = segment.final_path;
        segment.temporary_path += ".tmp";

        std::ofstream stream(segment.temporary_path, std::ios::out | std::ios::trunc | std::ios::binary);
        if (!stream) {
            spdlog::error("failed to create temporary network WAL segment: path={}",
                          segment.temporary_path.string());
            return false;
        }
        while (record_index < remaining.size() &&
               (segment.size == 0 || segment_bytes == 0 || segment.size < segment_bytes)) {
            stream << "0\t" << remaining[record_index].json_line;
            if (!stream) {
                spdlog::error("failed to write temporary network WAL segment: path={}",
                              segment.temporary_path.string());
                stream.close();
                fs::remove(segment.temporary_path, ec);
                return false;
            }
            remaining[record_index].path = segment.final_path;
            segment.size += remaining[record_index].json_line.size() + 2;
            ++record_index;
        }
        stream.flush();
        stream.close();
        if (!stream || !sync_file(segment.temporary_path)) {
            spdlog::error("failed to durably sync temporary network WAL segment: path={}",
                          segment.temporary_path.string());
            fs::remove(segment.temporary_path, ec);
            return false;
        }
        staged_segments.push_back(std::move(segment));
    } while (record_index < remaining.size());

    for (size_t i = 0; i < staged_segments.size(); ++i) {
        fs::rename(staged_segments[i].temporary_path, staged_segments[i].final_path, ec);
        if (ec) {
            spdlog::error("failed to publish temporary network WAL segment: temporary_path={} final_path={} error={}",
                          staged_segments[i].temporary_path.string(), staged_segments[i].final_path.string(),
                          ec.message());
            for (size_t j = i; j < staged_segments.size(); ++j) {
                std::error_code remove_error;
                fs::remove(staged_segments[j].temporary_path, remove_error);
            }
            return false;
        }
    }
    if (!sync_directory(wal_directory)) {
        spdlog::error("failed to durably publish network WAL generation: path={}", wal_directory.string());
        return false;
    }

    pending_ = std::move(remaining);
    current_wal_path_ = staged_segments.back().final_path;
    current_wal_size_ = staged_segments.back().size;
    current_wal_index_ = next_index;

    for (const fs::path& path : old_segments) {
        fs::remove(path, ec);
        if (ec) {
            spdlog::warn("failed to remove old network WAL segment after publishing replacement: path={} error={}",
                         path.string(), ec.message());
            ec.clear();
        }
    }
    if (!sync_directory(wal_directory)) {
        spdlog::warn("failed to sync network WAL directory after old generation cleanup: path={}",
                     wal_directory.string());
    }
    return true;
}

bool NetworkEventSink::ack_pending(size_t acked_count) {
    std::lock_guard<std::mutex> lock(pending_mutex_);
    return rewrite_wal_locked(acked_count);
}

bool NetworkEventSink::flush_pending() {
    std::vector<std::string> batch;
    {
        std::lock_guard<std::mutex> lock(pending_mutex_);
        const size_t count = std::min<size_t>(pending_.size(), config_.network_batch_size);
        batch.reserve(count);
        for (size_t i = 0; i < count; ++i) {
            batch.push_back(pending_[i].json_line);
        }
    }
    if (batch.empty()) {
        return true;
    }
    if (!post_batch(batch)) {
        send_errors_.fetch_add(1);
        return false;
    }
    sent_events_.fetch_add(batch.size());
    spdlog::debug("network sink sent batch: count={} endpoint={}", batch.size(), config_.network_endpoint);
    if (!ack_pending(batch.size())) {
        spdlog::warn("network sink retained acknowledged batch because WAL replacement was not durable: count={}",
                     batch.size());
        return false;
    }
    return true;
}

bool NetworkEventSink::post_batch(const std::vector<std::string>& batch) const {
#if defined(__linux__)
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* result = nullptr;
    const std::string port = std::to_string(endpoint_.port);
    if (::getaddrinfo(endpoint_.host.c_str(), port.c_str(), &hints, &result) != 0 || !result) {
        spdlog::warn("network sink DNS resolution failed: host={} port={}", endpoint_.host, endpoint_.port);
        return false;
    }
    const int fd = ::socket(result->ai_family, result->ai_socktype | SOCK_CLOEXEC, result->ai_protocol);
    if (fd < 0) {
        ::freeaddrinfo(result);
        spdlog::warn("network sink socket creation failed");
        return false;
    }
    timeval timeout{};
    timeout.tv_sec = static_cast<time_t>(config_.network_request_timeout_ms / 1000);
    timeout.tv_usec = static_cast<suseconds_t>((config_.network_request_timeout_ms % 1000) * 1000);
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    const bool connected = connect_with_timeout(fd, result);
    ::freeaddrinfo(result);
    if (!connected) {
        ::close(fd);
        spdlog::warn("network sink connect failed: host={} port={} timeout_ms={}", endpoint_.host, endpoint_.port,
                     config_.network_connect_timeout_ms);
        return false;
    }

    std::string body = "[";
    for (size_t i = 0; i < batch.size(); ++i) {
        if (i != 0) {
            body.push_back(',');
        }
        std::string item = batch[i];
        if (!item.empty() && item.back() == '\n') {
            item.pop_back();
        }
        body += item;
    }
    body.push_back(']');

    std::ostringstream request;
    request << "POST " << endpoint_.path << " HTTP/1.1\r\n"
            << "Host: " << endpoint_.host << "\r\n"
            << "Content-Type: application/json\r\n"
            << "Content-Length: " << body.size() << "\r\n"
            << "Connection: close\r\n\r\n"
            << body;
    const std::string payload = request.str();
    const char* data = payload.data();
    size_t remaining = payload.size();
    while (remaining > 0) {
        const ssize_t sent = ::send(fd, data, remaining, MSG_NOSIGNAL);
        if (sent <= 0) {
            ::close(fd);
            spdlog::warn("network sink send failed: endpoint={}", config_.network_endpoint);
            return false;
        }
        data += sent;
        remaining -= static_cast<size_t>(sent);
    }
    char response[128]{};
    const ssize_t received = ::recv(fd, response, sizeof(response) - 1, 0);
    ::close(fd);
    const bool ok = received > 0 && std::string(response, static_cast<size_t>(received)).rfind("HTTP/1.1 2", 0) == 0;
    if (!ok) {
        spdlog::warn("network sink received non-success response: endpoint={} received_bytes={}",
                     config_.network_endpoint, received);
    }
    return ok;
#else
    (void)batch;
    return false;
#endif
}

bool NetworkEventSink::connect_with_timeout(int fd, const addrinfo* target) const {
#if defined(__linux__)
    const int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags >= 0) {
        ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }
    if (::connect(fd, target->ai_addr, target->ai_addrlen) == 0) {
        if (flags >= 0) {
            ::fcntl(fd, F_SETFL, flags);
        }
        return true;
    }
    if (errno != EINPROGRESS) {
        return false;
    }
    pollfd pfd{};
    pfd.fd = fd;
    pfd.events = POLLOUT;
    const int rc = ::poll(&pfd, 1, static_cast<int>(config_.network_connect_timeout_ms));
    if (rc <= 0 || (pfd.revents & POLLOUT) == 0) {
        return false;
    }
    int error = 0;
    socklen_t error_len = sizeof(error);
    if (::getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &error_len) != 0 || error != 0) {
        return false;
    }
    if (flags >= 0) {
        ::fcntl(fd, F_SETFL, flags);
    }
    return true;
#else
    (void)fd;
    (void)target;
    return false;
#endif
}

NetworkEventSink::Endpoint NetworkEventSink::parse_endpoint() const {
    Endpoint endpoint;
    std::string value = config_.network_endpoint;
    const std::string prefix = "http://";
    if (value.rfind(prefix, 0) != 0) {
        return endpoint;
    }
    value.erase(0, prefix.size());
    const size_t slash = value.find('/');
    const std::string host_port = slash == std::string::npos ? value : value.substr(0, slash);
    endpoint.path = slash == std::string::npos ? "/events" : value.substr(slash);
    const size_t colon = host_port.rfind(':');
    endpoint.host = colon == std::string::npos ? host_port : host_port.substr(0, colon);
    if (colon != std::string::npos) {
        endpoint.port = static_cast<uint16_t>(std::stoi(host_port.substr(colon + 1)));
    }
    endpoint.valid = !endpoint.host.empty() && !endpoint.path.empty();
    return endpoint;
}

uint64_t NetworkEventSink::wal_bytes() const {
    uint64_t total = 0;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(config_.network_wal_path, ec)) {
        if (entry.is_regular_file() && entry.path().extension() == ".wal") {
            total += entry.file_size(ec);
        }
    }
    return total;
}

bool NetworkEventSink::wal_over_limit() const {
    return wal_bytes() >= config_.network_wal_max_mb * 1024ULL * 1024ULL;
}

std::filesystem::path NetworkEventSink::next_wal_path() {
    std::ostringstream name;
    name << "events_" << std::setw(6) << std::setfill('0') << current_wal_index_++ << ".wal";
    return fs::path(config_.network_wal_path) / name.str();
}

void NetworkEventSink::enforce_wal_limit() {
    if (!wal_over_limit()) {
        return;
    }
    spdlog::warn("network WAL over limit, rewriting pending WAL: wal_bytes={} max_mb={}", wal_bytes(),
                 config_.network_wal_max_mb);
    std::lock_guard<std::mutex> lock(pending_mutex_);
    if (!rewrite_wal_locked(0)) {
        spdlog::warn("network WAL compaction skipped because replacement generation was not durable");
    }
}

} // namespace lisysm
