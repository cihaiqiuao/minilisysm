#pragma once

#include "lisysm/config.hpp"
#include "lisysm/event_store.hpp"
#include "lisysm/meminfo_collector.hpp"
#include "lisysm/rule_engine.hpp"
#include "lisysm/spsc_ring_buffer.hpp"

#include <atomic>
#include <memory>
#include <thread>

namespace lisysm {

class Monitor {
public:
    explicit Monitor(MonitorConfig config);
    ~Monitor();

    Monitor(const Monitor&) = delete;
    Monitor& operator=(const Monitor&) = delete;

    bool start();
    void stop();
    bool running() const { return running_.load(); }

private:
    void collect_loop();
    bool publish_event(const InternalEvent& event);
    void publish_started_event();

    MonitorConfig config_;
    SpscRingBuffer<InternalEvent> queue_;
    EventStore store_;
    MeminfoCollector meminfo_;
    RuleEngine rules_;
    std::atomic<bool> running_{false};
    std::thread collector_;
    uint64_t next_sequence_{1};
};

} // namespace lisysm
