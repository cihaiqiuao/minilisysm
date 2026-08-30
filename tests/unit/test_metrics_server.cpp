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

#define CHECK(condition)                                                                                               \
    do {                                                                                                               \
        if (!(condition)) {                                                                                            \
            std::cerr << "check failed: " #condition << " at line " << __LINE__ << "\n";                               \
            return EXIT_FAILURE;                                                                                       \
        }                                                                                                              \
    } while (false)

namespace {

std::string http_get(uint16_t port, const char* path, const char* source_address = nullptr) {
#if defined(__linux__)
    const int fd = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        return {};
    }
    if (source_address != nullptr) {
        sockaddr_in source{};
        source.sin_family = AF_INET;
        source.sin_port = 0;
        if (::inet_pton(AF_INET, source_address, &source.sin_addr) != 1 ||
            ::bind(fd, reinterpret_cast<const sockaddr*>(&source), sizeof(source)) != 0) {
            ::close(fd);
            return {};
        }
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
    std::string response;
    char buffer[2048]{};
    ssize_t n = 0;
    while ((n = ::recv(fd, buffer, sizeof(buffer), 0)) > 0) {
        response.append(buffer, static_cast<size_t>(n));
    }
    ::close(fd);
    return response;
#else
    (void)port;
    (void)path;
    return {};
#endif
}

} // namespace

int main() {
#if !defined(__linux__)
    return EXIT_SUCCESS;
#else
    lisysm::MonitorConfig config;
    config.metrics_enable = true;
    config.metrics_bind_host = "127.0.0.1";
    config.metrics_port = 19108;

    lisysm::MetricsServer server(config, [] { return std::string("# TYPE minilisysm_up gauge\nminilisysm_up 1\n"); });
    CHECK(server.start());
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    const std::string metrics = http_get(config.metrics_port, "/metrics");
    CHECK(metrics.find("HTTP/1.1 200 OK") != std::string::npos);
    CHECK(metrics.find("minilisysm_up 1") != std::string::npos);

    const std::string health = http_get(config.metrics_port, "/healthz");
    CHECK(health.find("HTTP/1.1 200 OK") != std::string::npos);
    CHECK(health.find("ok") != std::string::npos);

    const std::string status = http_get(config.metrics_port, "/status");
    CHECK(status.find("HTTP/1.1 200 OK") != std::string::npos);
    CHECK(status.find("MiniLisySM 状态总览") != std::string::npos);
    CHECK(status.find("查看原始指标") != std::string::npos);
    CHECK(status.find("fetch(\"/metrics\"") != std::string::npos);

    const std::string root = http_get(config.metrics_port, "/");
    CHECK(root.find("HTTP/1.1 200 OK") != std::string::npos);
    CHECK(root.find("MiniLisySM 状态总览") != std::string::npos);

    server.stop();

    lisysm::MonitorConfig restricted_config = config;
    restricted_config.metrics_port = 19109;
    restricted_config.metrics_allowed_clients = {"127.0.0.1"};
    lisysm::MetricsServer restricted_server(
        restricted_config, [] { return std::string("# TYPE minilisysm_up gauge\nminilisysm_up 1\n"); });
    CHECK(restricted_server.start());
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    CHECK(http_get(restricted_config.metrics_port, "/metrics").find("HTTP/1.1 200 OK") != std::string::npos);
    CHECK(http_get(restricted_config.metrics_port, "/status").find("HTTP/1.1 200 OK") != std::string::npos);
    CHECK(http_get(restricted_config.metrics_port, "/healthz").find("HTTP/1.1 200 OK") != std::string::npos);
    CHECK(http_get(restricted_config.metrics_port, "/metrics", "127.0.0.2").find("HTTP/1.1 403 Forbidden") !=
          std::string::npos);
    restricted_server.stop();

    lisysm::MonitorConfig invalid_config = config;
    invalid_config.metrics_port = 19110;
    invalid_config.metrics_allowed_clients = {"not-an-ip"};
    lisysm::MetricsServer invalid_server(invalid_config, [] { return std::string{}; });
    CHECK(!invalid_server.start());
    return EXIT_SUCCESS;
#endif
}
