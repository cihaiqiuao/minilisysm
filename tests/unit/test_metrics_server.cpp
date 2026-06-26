#include "minilisysm/core/config.hpp"
#include "minilisysm/runtime/metrics_server.hpp"

#include <chrono>
#include <cstdlib>
#include <iostream>
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

std::string http_get(uint16_t port, const char* path)
{
#if defined(__linux__)
    const int fd = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        return {};
    }
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    ::inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    if (::connect(fd, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) != 0) {
        ::close(fd);
        return {};
    }
    const std::string request = std::string("GET ") + path + " HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n";
    ::send(fd, request.data(), request.size(), MSG_NOSIGNAL);
    char buffer[2048]{};
    const ssize_t n = ::recv(fd, buffer, sizeof(buffer) - 1, 0);
    ::close(fd);
    return n > 0 ? std::string(buffer, static_cast<size_t>(n)) : std::string{};
#else
    (void)port;
    (void)path;
    return {};
#endif
}

} // namespace

int main()
{
#if !defined(__linux__)
    return EXIT_SUCCESS;
#else
    lisysm::MonitorConfig config;
    config.metrics_enable = true;
    config.metrics_bind_host = "127.0.0.1";
    config.metrics_port = 19108;

    lisysm::MetricsServer server(config, [] {
        return std::string("# TYPE minilisysm_up gauge\nminilisysm_up 1\n");
    });
    CHECK(server.start());
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    const std::string metrics = http_get(config.metrics_port, "/metrics");
    CHECK(metrics.find("HTTP/1.1 200 OK") != std::string::npos);
    CHECK(metrics.find("minilisysm_up 1") != std::string::npos);

    const std::string health = http_get(config.metrics_port, "/healthz");
    CHECK(health.find("HTTP/1.1 200 OK") != std::string::npos);
    CHECK(health.find("ok") != std::string::npos);

    server.stop();
    return EXIT_SUCCESS;
#endif
}
