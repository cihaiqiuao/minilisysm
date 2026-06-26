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

#define CHECK(condition)                                                                            \
    do {                                                                                            \
        if (!(condition)) {                                                                         \
            std::cerr << "check failed: " #condition << " at line " << __LINE__ << "\n";          \
            return EXIT_FAILURE;                                                                    \
        }                                                                                           \
    } while (false)

namespace {

#if defined(__linux__)
void run_server(uint16_t port, std::atomic<bool>* ready, std::atomic<bool>* received)
{
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
    if (::bind(fd, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) != 0 ||
        ::listen(fd, 1) != 0) {
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
#endif

} // namespace

int main()
{
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
