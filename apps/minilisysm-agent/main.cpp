#include "minilisysm/core/config.hpp"
#include "minilisysm/runtime/monitor.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <thread>

namespace {
std::atomic<bool> g_stop{false};

void handle_signal(int)
{
    g_stop.store(true);
}
} // namespace

int main(int argc, char** argv)
{
    const std::string config_path = argc > 1 ? argv[1] : "configs/lisysm_monitor.ini";
    lisysm::MonitorConfig config = lisysm::ConfigLoader::load_or_default(config_path);
    std::string error;
    lisysm::ConfigLoader::validate(config, &error);
    if (!error.empty()) {
        std::cerr << "config warning: " << error << "\n";
    }

    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    lisysm::Monitor monitor(config);
    if (!monitor.start()) {
        std::cerr << "monitor disabled or failed to start\n";
        return 1;
    }

    while (!g_stop.load()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    monitor.stop();
    return 0;
}
