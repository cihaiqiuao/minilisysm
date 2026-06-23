#include "lisysm/monitor.hpp"
#include "lisysm/thread_policy.hpp"
#include "lisysm/time.hpp"

#include <chrono>
#include <iostream>
#include <utility>

namespace lisysm {

Monitor::Monitor(MonitorConfig config)
    : config_(std::move(config)),
      queue_(config_.event_queue_capacity),
      store_(config_, queue_),
      rules_(config_)
{
}

Monitor::~Monitor()
{
    stop();
}

bool Monitor::start()
{
    if (!config_.enable) {
        return false;
    }
    if (!store_.start()) {
        return false;
    }
    running_.store(true);
    publish_started_event();
    collector_ = std::thread(&Monitor::collect_loop, this);
    return true;
}

void Monitor::stop()
{
    running_.store(false);
    if (collector_.joinable()) {
        collector_.join();
    }
    store_.stop();
}

void Monitor::collect_loop()
{
    std::string ignored;
    set_current_thread_affinity(config_.fast_collector_cpu, &ignored);
    set_current_thread_nice(config_.fast_collector_nice, &ignored);

    const auto interval = std::chrono::milliseconds(config_.fast_collect_interval_ms);
    while (running_.load()) {
        const uint64_t start = monotonic_ms();
        const MeminfoSample sample = meminfo_.collect();
        if (auto event = rules_.evaluate_memory(sample)) {
            publish_event(*event);
        }
        const uint64_t elapsed = monotonic_ms() - start;
        if (elapsed > 200) {
            InternalEvent overrun;
            overrun.event_type = EventType::MonitorOverrun;
            overrun.level = EventLevel::Warning;
            overrun.value = static_cast<double>(elapsed);
            overrun.warning_threshold = 200.0;
            publish_event(overrun);
        }
        std::this_thread::sleep_for(interval);
    }
}

bool Monitor::publish_event(const InternalEvent& source)
{
    InternalEvent event = source;
    event.sequence = next_sequence_++;
    event.realtime_ms = realtime_ms();
    event.monotonic_ms = monotonic_ms();
    event.boottime_ms = boottime_ms();
    if (event.last_seen_ms == 0) {
        event.last_seen_ms = event.realtime_ms;
    }
    if (event.first_seen_ms == 0) {
        event.first_seen_ms = event.realtime_ms;
    }
    return queue_.push(event, event.level);
}

void Monitor::publish_started_event()
{
    InternalEvent event;
    event.event_type = EventType::MonitorStarted;
    event.level = EventLevel::Info;
    event.status = EventStatus::Active;
    publish_event(event);
}

} // namespace lisysm
