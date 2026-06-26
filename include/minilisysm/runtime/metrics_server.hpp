#pragma once

#include "minilisysm/core/config.hpp"

#include <atomic>
#include <functional>
#include <string>
#include <thread>

namespace lisysm {

class MetricsServer {
public:
    using RenderCallback = std::function<std::string()>;

    MetricsServer(const MonitorConfig& config, RenderCallback render);
    ~MetricsServer();

    MetricsServer(const MetricsServer&) = delete;
    MetricsServer& operator=(const MetricsServer&) = delete;

    bool start();
    void stop();
    bool running() const { return running_.load(); }

private:
    void run();
    bool open_listener();
    void close_listener();
    void handle_client(int client_fd);
    std::string response_for_request(const std::string& request) const;

    const MonitorConfig& config_;
    RenderCallback render_;
    std::atomic<bool> running_{false};
    std::thread worker_;
    int listen_fd_{-1};
};

} // namespace lisysm
