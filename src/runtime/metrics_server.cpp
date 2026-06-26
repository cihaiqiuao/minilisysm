#include "minilisysm/runtime/metrics_server.hpp"

#include <chrono>
#include <cstring>
#include <sstream>
#include <string>
#include <thread>
#include <utility>

#if defined(__linux__)
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace lisysm {
namespace {

std::string http_response(int status, const char* status_text, const char* content_type, const std::string& body)
{
    std::ostringstream output;
    output << "HTTP/1.1 " << status << ' ' << status_text << "\r\n"
           << "Content-Type: " << content_type << "\r\n"
           << "Content-Length: " << body.size() << "\r\n"
           << "Connection: close\r\n\r\n"
           << body;
    return output.str();
}

} // namespace

MetricsServer::MetricsServer(const MonitorConfig& config, RenderCallback render)
    : config_(config),
      render_(std::move(render))
{
}

MetricsServer::~MetricsServer()
{
    stop();
}

bool MetricsServer::start()
{
    if (!config_.metrics_enable) {
        return true;
    }
    if (!open_listener()) {
        return false;
    }
    running_.store(true);
    worker_ = std::thread(&MetricsServer::run, this);
    return true;
}

void MetricsServer::stop()
{
    running_.store(false);
    close_listener();
    if (worker_.joinable()) {
        worker_.join();
    }
}

void MetricsServer::run()
{
#if defined(__linux__)
    while (running_.load()) {
        pollfd pfd{};
        pfd.fd = listen_fd_;
        pfd.events = POLLIN;
        const int rc = ::poll(&pfd, 1, 200);
        if (rc <= 0 || (pfd.revents & POLLIN) == 0) {
            continue;
        }
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        const int client_fd = ::accept(listen_fd_, reinterpret_cast<sockaddr*>(&client_addr), &client_len);
        if (client_fd < 0) {
            continue;
        }
        handle_client(client_fd);
    }
#else
    while (running_.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
#endif
}

bool MetricsServer::open_listener()
{
#if defined(__linux__)
    listen_fd_ = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (listen_fd_ < 0) {
        return false;
    }
    int reuse = 1;
    ::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(config_.metrics_port);
    if (config_.metrics_bind_host.empty() || config_.metrics_bind_host == "0.0.0.0") {
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
    } else if (::inet_pton(AF_INET, config_.metrics_bind_host.c_str(), &addr.sin_addr) != 1) {
        close_listener();
        return false;
    }
    if (::bind(listen_fd_, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) != 0) {
        close_listener();
        return false;
    }
    if (::listen(listen_fd_, 16) != 0) {
        close_listener();
        return false;
    }
    return true;
#else
    return false;
#endif
}

void MetricsServer::close_listener()
{
#if defined(__linux__)
    if (listen_fd_ >= 0) {
        ::shutdown(listen_fd_, SHUT_RDWR);
        ::close(listen_fd_);
        listen_fd_ = -1;
    }
#endif
}

void MetricsServer::handle_client(int client_fd)
{
#if defined(__linux__)
    char buffer[1024]{};
    const ssize_t n = ::recv(client_fd, buffer, sizeof(buffer) - 1, 0);
    std::string response;
    if (n > 0) {
        response = response_for_request(std::string(buffer, static_cast<size_t>(n)));
    } else {
        response = http_response(400, "Bad Request", "text/plain; charset=utf-8", "bad request\n");
    }
    const char* data = response.data();
    size_t remaining = response.size();
    while (remaining > 0) {
        const ssize_t sent = ::send(client_fd, data, remaining, MSG_NOSIGNAL);
        if (sent <= 0) {
            break;
        }
        data += sent;
        remaining -= static_cast<size_t>(sent);
    }
    ::close(client_fd);
#else
    (void)client_fd;
#endif
}

std::string MetricsServer::response_for_request(const std::string& request) const
{
    if (request.rfind("GET /metrics ", 0) == 0 || request.rfind("GET /metrics?", 0) == 0) {
        return http_response(200, "OK", "text/plain; version=0.0.4; charset=utf-8", render_());
    }
    if (request.rfind("GET /healthz ", 0) == 0) {
        return http_response(200, "OK", "text/plain; charset=utf-8", "ok\n");
    }
    return http_response(404, "Not Found", "text/plain; charset=utf-8", "not found\n");
}

} // namespace lisysm
