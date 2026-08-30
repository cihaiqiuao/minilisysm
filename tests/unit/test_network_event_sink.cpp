#include "minilisysm/core/config.hpp"
#include "minilisysm/core/event.hpp"
#include "minilisysm/storage/network_event_sink.hpp"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <thread>

#if defined(__linux__)
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#define CHECK(condition)                                                                                               \
    do {                                                                                                               \
        if (!(condition)) {                                                                                            \
            std::cerr << "check failed: " #condition << " at line " << __LINE__ << "\n";                               \
            return EXIT_FAILURE;                                                                                       \
        }                                                                                                              \
    } while (false)

namespace {

#if defined(__linux__)
void run_server(uint16_t port, std::atomic<bool>* ready, std::atomic<bool>* received) {
    const int fd = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        ready->store(true);
        return;
    }
    int reuse = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::bind(fd, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) != 0 || ::listen(fd, 1) != 0) {
        ready->store(true);
        ::close(fd);
        return;
    }
    ready->store(true);
    const int client = ::accept(fd, nullptr, nullptr);
    if (client >= 0) {
        char buffer[4096]{};
        const ssize_t n = ::recv(client, buffer, sizeof(buffer) - 1, 0);
        if (n > 0 && std::string(buffer, static_cast<size_t>(n)).find("POST /events") != std::string::npos) {
            received->store(true);
        }
        const std::string response = "HTTP/1.1 204 No Content\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
        ::send(client, response.data(), response.size(), MSG_NOSIGNAL);
        ::close(client);
    }
    ::close(fd);
}

void run_delayed_response_server(uint16_t port, std::atomic<bool>* ready, std::atomic<bool>* received,
                                 std::atomic<bool>* allow_response) {
    const int fd = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        ready->store(true);
        return;
    }
    int reuse = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::bind(fd, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) != 0 || ::listen(fd, 1) != 0) {
        ready->store(true);
        ::close(fd);
        return;
    }
    ready->store(true);
    const int client = ::accept(fd, nullptr, nullptr);
    if (client >= 0) {
        char buffer[4096]{};
        const ssize_t n = ::recv(client, buffer, sizeof(buffer) - 1, 0);
        if (n > 0 && std::string(buffer, static_cast<size_t>(n)).find("POST /events") != std::string::npos) {
            received->store(true);
        }
        for (int attempt = 0; attempt < 5000 && !allow_response->load(); ++attempt) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        if (allow_response->load()) {
            const std::string response = "HTTP/1.1 204 No Content\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
            ::send(client, response.data(), response.size(), MSG_NOSIGNAL);
        }
        ::close(client);
    }
    ::close(fd);
}
#endif

} // namespace

int main() {
#if !defined(__linux__)
    return EXIT_SUCCESS;
#else
    namespace fs = std::filesystem;
    {
        const fs::path offline_wal_path = "/tmp/minilisysm-network-sink-offline-test";
        fs::remove_all(offline_wal_path);
        lisysm::MonitorConfig offline_config;
        offline_config.device_id = "network-offline-test";
        offline_config.network_sink_enable = true;
        offline_config.network_endpoint = "http://127.0.0.1:9/events";
        offline_config.network_wal_path = offline_wal_path.string();
        offline_config.network_batch_size = 4;
        offline_config.network_flush_interval_ms = 10;
        offline_config.network_retry_base_ms = 10;
        offline_config.network_request_timeout_ms = 10;
        offline_config.network_connect_timeout_ms = 10;
        offline_config.sink_idle_sleep_ms = 1;

        lisysm::NetworkEventSink offline_sink(offline_config);
        lisysm::SpscRingBuffer<lisysm::InternalEvent>* offline_input =
            offline_sink.add_input_queue(offline_config.event_queue_capacity);
        CHECK(offline_input != nullptr);
        CHECK(offline_sink.start());
        lisysm::InternalEvent offline_event;
        offline_event.sequence = 7;
        offline_event.event_type = lisysm::EventType::MonitorStarted;
        offline_event.level = lisysm::EventLevel::Info;
        CHECK(offline_input->push(offline_event, offline_event.level));
        std::this_thread::sleep_for(std::chrono::milliseconds(80));
        offline_sink.stop();

        std::string offline_content;
        for (const auto& entry : fs::directory_iterator(offline_wal_path)) {
            if (entry.is_regular_file() && entry.path().extension() == ".wal") {
                std::ifstream wal(entry.path(), std::ios::binary);
                offline_content.append((std::istreambuf_iterator<char>(wal)), std::istreambuf_iterator<char>());
            }
        }
        CHECK(offline_content.find("\"event_id\":\"network-offline-test-7\"") != std::string::npos);
        fs::remove_all(offline_wal_path);
    }

    {
        const fs::path write_failure_wal_path = "/tmp/minilisysm-network-sink-write-failure-test";
        fs::remove_all(write_failure_wal_path);
        CHECK(fs::create_directories(write_failure_wal_path / "events_000001.wal"));

        lisysm::MonitorConfig failure_config;
        failure_config.device_id = "network-write-failure-test";
        failure_config.network_sink_enable = true;
        failure_config.network_endpoint = "http://127.0.0.1:9/events";
        failure_config.network_wal_path = write_failure_wal_path.string();
        failure_config.network_batch_size = 1;
        failure_config.network_flush_interval_ms = 10;
        failure_config.network_retry_base_ms = 10;
        failure_config.network_request_timeout_ms = 10;
        failure_config.network_connect_timeout_ms = 10;
        failure_config.sink_idle_sleep_ms = 1;

        lisysm::NetworkEventSink failure_sink(failure_config);
        auto* failure_input = failure_sink.add_input_queue(failure_config.event_queue_capacity);
        CHECK(failure_input != nullptr);
        CHECK(failure_sink.start());

        lisysm::InternalEvent failure_event;
        failure_event.sequence = 6;
        failure_event.event_type = lisysm::EventType::MonitorStarted;
        failure_event.level = lisysm::EventLevel::Info;
        CHECK(failure_input->push(failure_event, failure_event.level));

        for (int attempt = 0; attempt < 200; ++attempt) {
            const lisysm::SinkStats current = failure_sink.stats();
            if (current.queue_depth == 0 && (current.write_errors > 0 || current.wal_pending_events > 0)) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        failure_sink.stop();

        const lisysm::SinkStats failure_stats = failure_sink.stats();
        CHECK(failure_stats.write_errors == 1);
        CHECK(failure_stats.accepted_events == 0);
        CHECK(failure_stats.wal_pending_events == 0);
        fs::remove_all(write_failure_wal_path);
    }

    {
        const uint16_t failure_port = 19181;
        const fs::path failure_wal_path = "/tmp/minilisysm-network-sink-publish-failure-test";
        fs::remove_all(failure_wal_path);

        std::atomic<bool> ready{false};
        std::atomic<bool> received{false};
        std::atomic<bool> allow_response{false};
        std::thread server(run_delayed_response_server, failure_port, &ready, &received, &allow_response);
        while (!ready.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        lisysm::MonitorConfig failure_config;
        failure_config.device_id = "network-publish-failure-test";
        failure_config.network_sink_enable = true;
        failure_config.network_endpoint = "http://127.0.0.1:" + std::to_string(failure_port) + "/events";
        failure_config.network_wal_path = failure_wal_path.string();
        failure_config.network_batch_size = 1;
        failure_config.network_flush_interval_ms = 10;
        failure_config.network_retry_base_ms = 10;
        failure_config.network_request_timeout_ms = 1000;
        failure_config.network_connect_timeout_ms = 1000;
        failure_config.sink_idle_sleep_ms = 1;

        lisysm::NetworkEventSink failure_sink(failure_config);
        auto* failure_input = failure_sink.add_input_queue(failure_config.event_queue_capacity);
        CHECK(failure_input != nullptr);

        lisysm::InternalEvent failure_event;
        failure_event.sequence = 8;
        failure_event.event_type = lisysm::EventType::MonitorStarted;
        failure_event.level = lisysm::EventLevel::Info;
        CHECK(failure_input->push(failure_event, failure_event.level));
        failure_event.sequence = 9;
        CHECK(failure_input->push(failure_event, failure_event.level));
        CHECK(failure_sink.start());
        for (int attempt = 0; attempt < 2000 && !received.load(); ++attempt) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        CHECK(received.load());

        fs::path old_segment;
        for (const auto& entry : fs::directory_iterator(failure_wal_path)) {
            if (entry.is_regular_file() && entry.path().extension() == ".wal") {
                old_segment = entry.path();
                break;
            }
        }
        CHECK(!old_segment.empty());
        const fs::path blocked_publish_path = failure_wal_path / "events_000002.wal";
        CHECK(fs::create_directory(blocked_publish_path));
        allow_response.store(true);

        for (int attempt = 0; attempt < 2000; ++attempt) {
            const lisysm::SinkStats current = failure_sink.stats();
            if (current.wal_pending_events == 0 || current.retry_count > 0) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        server.join();

        const lisysm::SinkStats failed_publish_stats = failure_sink.stats();
        CHECK(failed_publish_stats.wal_pending_events == 2);
        CHECK(failed_publish_stats.retry_count > 0);
        CHECK(fs::exists(old_segment));
        CHECK(!fs::exists(failure_wal_path / "events_000002.wal.tmp"));
        std::ifstream old_wal(old_segment, std::ios::binary);
        const std::string old_content((std::istreambuf_iterator<char>(old_wal)), std::istreambuf_iterator<char>());
        CHECK(old_content.find("\"event_id\":\"network-publish-failure-test-8\"") != std::string::npos);
        CHECK(old_content.find("\"event_id\":\"network-publish-failure-test-9\"") != std::string::npos);

        failure_sink.stop();
        fs::remove_all(failure_wal_path);
    }

    const uint16_t port = 19180;
    const fs::path wal_path = "/tmp/minilisysm-network-sink-test";
    fs::remove_all(wal_path);

    std::atomic<bool> ready{false};
    std::atomic<bool> received{false};
    std::thread server(run_server, port, &ready, &received);
    while (!ready.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    lisysm::MonitorConfig config;
    config.device_id = "network-test";
    config.network_sink_enable = true;
    config.network_endpoint = "http://127.0.0.1:" + std::to_string(port) + "/events";
    config.network_wal_path = wal_path.string();
    config.network_batch_size = 4;
    config.network_flush_interval_ms = 10;
    config.network_retry_base_ms = 10;
    config.sink_idle_sleep_ms = 1;

    lisysm::NetworkEventSink sink(config);
    lisysm::SpscRingBuffer<lisysm::InternalEvent>* input = sink.add_input_queue(config.event_queue_capacity);
    CHECK(input != nullptr);
    CHECK(sink.start());

    lisysm::InternalEvent event;
    event.sequence = 42;
    event.event_type = lisysm::EventType::MonitorStarted;
    event.level = lisysm::EventLevel::Info;
    CHECK(input->push(event, event.level));

    for (int i = 0; i < 200 && !received.load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    sink.stop();
    server.join();

    CHECK(received.load());
    const lisysm::SinkStats stats = sink.stats();
    CHECK(stats.sent_events >= 1);
    std::string content;
    for (const auto& entry : fs::directory_iterator(wal_path)) {
        if (entry.is_regular_file() && entry.path().extension() == ".wal") {
            std::ifstream wal(entry.path(), std::ios::binary);
            content.append((std::istreambuf_iterator<char>(wal)), std::istreambuf_iterator<char>());
        }
    }
    CHECK(content.empty());
    fs::remove_all(wal_path);
    return EXIT_SUCCESS;
#endif
}
