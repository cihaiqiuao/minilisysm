#include "minilisysm/runtime/event_dispatcher.hpp"
#include "minilisysm/runtime/thread_policy.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <chrono>
#include <string>
#include <utility>

namespace lisysm {

EventDispatcher::EventDispatcher(const MonitorConfig& config, SpscRingBuffer<InternalEvent>& source_queue,
                                 std::vector<SpscRingBuffer<InternalEvent>*> sink_queues)
    : config_(config), source_queue_(source_queue), sink_queues_(std::move(sink_queues)) {}

EventDispatcher::~EventDispatcher() {
    stop();
}

bool EventDispatcher::start() {
    running_.store(true);
    worker_ = std::thread(&EventDispatcher::run, this);
    spdlog::debug("event dispatcher started: sink_queues={}", sink_queues_.size());
    return true;
}

void EventDispatcher::stop() {
    const bool was_running = running_.exchange(false);
    if (worker_.joinable()) {
        worker_.join();
    }
    if (was_running) {
        spdlog::debug("event dispatcher stopped");
    }
}

DispatcherStats EventDispatcher::stats() const {
    return DispatcherStats{
        consumed_events_.load(),
        sink_queue_push_failures_.load(),
    };
}

void EventDispatcher::run() {
    std::string ignored;
    if (!set_current_thread_affinity(config_.persist_thread_cpu, &ignored) && config_.persist_thread_cpu >= 0) {
        spdlog::warn("failed to set dispatcher CPU affinity: cpu={} reason={}", config_.persist_thread_cpu, ignored);
    }
    if (!set_current_thread_nice(config_.background_nice, &ignored)) {
        spdlog::warn("failed to set dispatcher nice: nice={} reason={}", config_.background_nice, ignored);
    }

    while (running_.load()) {
        InternalEvent event;
        if (!source_queue_.pop(event)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(config_.dispatcher_idle_sleep_ms));
            continue;
        }
        dispatch(event);
    }
    drain_source();
}

void EventDispatcher::drain_source() {
    InternalEvent event;
    while (source_queue_.pop(event)) {
        dispatch(event);
    }
}

void EventDispatcher::dispatch(const InternalEvent& event) {
    consumed_events_.fetch_add(1);
    for (SpscRingBuffer<InternalEvent>* queue : sink_queues_) {
        if (queue && !queue->push(event, event.level)) {
            const uint64_t failures = sink_queue_push_failures_.fetch_add(1) + 1;
            if (failures == 1 || failures % 1000 == 0) {
                spdlog::warn("dispatcher failed to push event into sink queue: failures={} event_type={} level={}",
                             failures, to_string(event.event_type), to_string(event.level));
            }
        }
    }
}

EventDispatcherGroup::EventDispatcherGroup(const MonitorConfig& config,
                                           std::vector<SpscRingBuffer<InternalEvent>*>& source_queues,
                                           std::vector<std::unique_ptr<EventSink>> sinks)
    : config_(config), source_queues_(source_queues), sinks_(std::move(sinks)) {
    for (SpscRingBuffer<InternalEvent>* source_queue : source_queues_) {
        if (!source_queue) {
            continue;
        }
        std::vector<SpscRingBuffer<InternalEvent>*> sink_queues;
        sink_queues.reserve(sinks_.size());
        for (std::unique_ptr<EventSink>& sink : sinks_) {
            if (sink) {
                sink_queues.push_back(sink->add_input_queue(config_.event_queue_capacity));
            }
        }
        dispatchers_.push_back(std::make_unique<EventDispatcher>(config_, *source_queue, std::move(sink_queues)));
    }
}

EventDispatcherGroup::~EventDispatcherGroup() {
    stop();
}

bool EventDispatcherGroup::start() {
    spdlog::info("event dispatcher group starting: sinks={} dispatchers={}", sinks_.size(), dispatchers_.size());
    for (const std::unique_ptr<EventSink>& sink : sinks_) {
        if (sink && !sink->start()) {
            spdlog::error("event sink failed to start: name={}", sink->name());
            return false;
        }
        if (sink) {
            spdlog::info("event sink started: name={}", sink->name());
        }
    }
    for (const std::unique_ptr<EventDispatcher>& dispatcher : dispatchers_) {
        if (dispatcher && !dispatcher->start()) {
            spdlog::error("event dispatcher failed to start");
            return false;
        }
    }
    spdlog::info("event dispatcher group started");
    return true;
}

void EventDispatcherGroup::stop() {
    for (std::unique_ptr<EventDispatcher>& dispatcher : dispatchers_) {
        if (dispatcher) {
            dispatcher->stop();
        }
    }
    for (std::unique_ptr<EventSink>& sink : sinks_) {
        if (sink) {
            sink->stop();
            spdlog::info("event sink stopped: name={}", sink->name());
        }
    }
}

DispatcherStats EventDispatcherGroup::stats() const {
    DispatcherStats total;
    for (const std::unique_ptr<EventDispatcher>& dispatcher : dispatchers_) {
        if (!dispatcher) {
            continue;
        }
        const DispatcherStats current = dispatcher->stats();
        total.consumed_events += current.consumed_events;
        total.sink_queue_push_failures += current.sink_queue_push_failures;
    }
    for (const std::unique_ptr<EventSink>& sink : sinks_) {
        if (!sink) {
            continue;
        }
        const SinkStats current = sink->stats();
        total.sink_queue_dropped_events += current.dropped_events;
        total.sink_queue_dropped_critical_events += current.dropped_critical_events;
        total.sink_queue_reserve_reject_events += current.reserve_reject_events;
        total.sink_queue_depth += current.queue_depth;
        total.sink_queue_capacity += current.queue_capacity;
        total.sink_queue_high_watermark = std::max(total.sink_queue_high_watermark, current.queue_high_watermark);
    }
    return total;
}

std::vector<std::pair<std::string, SinkStats>> EventDispatcherGroup::sink_stats() const {
    std::vector<std::pair<std::string, SinkStats>> result;
    result.reserve(sinks_.size());
    for (const std::unique_ptr<EventSink>& sink : sinks_) {
        if (sink) {
            result.emplace_back(sink->name(), sink->stats());
        }
    }
    return result;
}

} // namespace lisysm
