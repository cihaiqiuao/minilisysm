#include "minilisysm/core/config.hpp"
#include "minilisysm/core/event.hpp"
#include "minilisysm/interfaces/event_sink.hpp"
#include "minilisysm/queue/spsc_ring_buffer.hpp"
#include "minilisysm/runtime/event_dispatcher.hpp"

#include <spdlog/spdlog.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#define CHECK(condition)                                                                                               \
    do {                                                                                                               \
        if (!(condition)) {                                                                                            \
            std::cerr << "check failed: " #condition << " at line " << __LINE__ << "\n";                               \
            return EXIT_FAILURE;                                                                                       \
        }                                                                                                              \
    } while (false)

namespace {

class FakeSink : public lisysm::EventSink {
  public:
    const char* name() const override {
        return "fake";
    }
    lisysm::SpscRingBuffer<lisysm::InternalEvent>* add_input_queue(size_t capacity) override {
        queues.push_back(std::make_unique<lisysm::SpscRingBuffer<lisysm::InternalEvent>>(capacity));
        return queues.back().get();
    }

    bool start() override {
        started = true;
        ++start_count;
        return true;
    }

    void stop() override {
        stopped = true;
        ++stop_count;
    }

    lisysm::SinkStats stats() const override {
        return {};
    }

    bool started{false};
    bool stopped{false};
    size_t start_count{0};
    size_t stop_count{0};
    std::vector<std::unique_ptr<lisysm::SpscRingBuffer<lisysm::InternalEvent>>> queues;
};

class BlockingSink : public lisysm::EventSink {
  public:
    const char* name() const override {
        return "blocking";
    }

    lisysm::SpscRingBuffer<lisysm::InternalEvent>* add_input_queue(size_t capacity) override {
        queue = std::make_unique<lisysm::SpscRingBuffer<lisysm::InternalEvent>>(capacity);
        return queue.get();
    }

    bool start() override {
        std::unique_lock<std::mutex> lock(mutex);
        start_entered = true;
        condition.notify_all();
        condition.wait(lock, [this]() { return release_start; });
        return true;
    }

    void stop() override {
        std::lock_guard<std::mutex> lock(mutex);
        ++stop_count;
        condition.notify_all();
    }

    lisysm::SinkStats stats() const override {
        return {};
    }

    void wait_for_start() {
        std::unique_lock<std::mutex> lock(mutex);
        condition.wait(lock, [this]() { return start_entered; });
    }

    bool wait_for_stop(std::chrono::milliseconds timeout) {
        std::unique_lock<std::mutex> lock(mutex);
        return condition.wait_for(lock, timeout, [this]() { return stop_count != 0; });
    }

    void allow_start_to_finish() {
        std::lock_guard<std::mutex> lock(mutex);
        release_start = true;
        condition.notify_all();
    }

    size_t stops() const {
        std::lock_guard<std::mutex> lock(mutex);
        return stop_count;
    }

    std::unique_ptr<lisysm::SpscRingBuffer<lisysm::InternalEvent>> queue;

  private:
    mutable std::mutex mutex;
    std::condition_variable condition;
    bool start_entered{false};
    bool release_start{false};
    size_t stop_count{0};
};

} // namespace

int main() {
    lisysm::MonitorConfig config;

    {
        lisysm::SpscRingBuffer<lisysm::InternalEvent> source_queue(8);
        std::vector<lisysm::SpscRingBuffer<lisysm::InternalEvent>*> source_queues{&source_queue};
        auto sink_owner = std::make_unique<BlockingSink>();
        BlockingSink* sink = sink_owner.get();
        std::vector<std::unique_ptr<lisysm::EventSink>> sinks;
        sinks.push_back(std::move(sink_owner));
        lisysm::EventDispatcherGroup group(config, source_queues, std::move(sinks));

        std::atomic<bool> start_result{false};
        std::thread starter([&]() { start_result.store(group.start()); });
        sink->wait_for_start();

        std::thread stopper([&]() { group.stop(); });
        const bool stopped_before_start_finished = sink->wait_for_stop(std::chrono::milliseconds(250));
        sink->allow_start_to_finish();
        starter.join();
        stopper.join();

        CHECK(start_result.load());
        CHECK(!stopped_before_start_finished);
        CHECK(sink->stops() == 1);
    }

    lisysm::SpscRingBuffer<lisysm::InternalEvent> source_queue(8);
    lisysm::SpscRingBuffer<lisysm::InternalEvent> first_sink_queue(8);
    lisysm::SpscRingBuffer<lisysm::InternalEvent> second_sink_queue(1);
    std::vector<lisysm::SpscRingBuffer<lisysm::InternalEvent>*> sink_queues{
        &first_sink_queue,
        &second_sink_queue,
    };

    lisysm::InternalEvent event;
    event.sequence = 1;
    event.event_type = lisysm::EventType::MemoryPressure;
    CHECK(source_queue.push(event, event.level));
    CHECK(second_sink_queue.push(event, event.level));

    {
        lisysm::EventDispatcher dispatcher(config, source_queue, sink_queues);
        CHECK(dispatcher.start());
        CHECK(dispatcher.start());
        dispatcher.stop();

        lisysm::InternalEvent out;
        CHECK(first_sink_queue.pop(out));
        CHECK(out.sequence == 1);
        const lisysm::DispatcherStats stats = dispatcher.stats();
        CHECK(stats.consumed_events == 1);
        CHECK(stats.sink_queue_push_failures == 1);
    }

    lisysm::SpscRingBuffer<lisysm::InternalEvent> fast_queue(8);
    lisysm::SpscRingBuffer<lisysm::InternalEvent> sched_queue(8);
    std::vector<lisysm::SpscRingBuffer<lisysm::InternalEvent>*> source_queues{&fast_queue, &sched_queue};

    auto sink_owner = std::make_unique<FakeSink>();
    FakeSink* sink = sink_owner.get();
    std::vector<std::unique_ptr<lisysm::EventSink>> sinks;
    sinks.push_back(std::move(sink_owner));

    lisysm::EventDispatcherGroup group(config, source_queues, std::move(sinks));
    CHECK(group.dispatcher_count() == 2);
    CHECK(group.sink_count() == 1);
    CHECK(sink->queues.size() == 2);

    lisysm::InternalEvent fast_event;
    fast_event.sequence = 10;
    lisysm::InternalEvent sched_event;
    sched_event.sequence = 20;
    CHECK(fast_queue.push(fast_event, fast_event.level));
    CHECK(sched_queue.push(sched_event, sched_event.level));

    CHECK(group.start());
    group.stop();

    CHECK(sink->started);
    CHECK(sink->stopped);
    CHECK(sink->start_count == 1);
    CHECK(sink->stop_count == 1);

    size_t received = 0;
    for (const auto& queue : sink->queues) {
        lisysm::InternalEvent out;
        while (queue->pop(out)) {
            CHECK(out.sequence == 10 || out.sequence == 20);
            ++received;
        }
    }
    CHECK(received == 2);
    const lisysm::DispatcherStats group_stats = group.stats();
    CHECK(group_stats.consumed_events == 2);

    spdlog::shutdown();
    group.stop();
    CHECK(sink->stop_count == 1);
    return EXIT_SUCCESS;
}
