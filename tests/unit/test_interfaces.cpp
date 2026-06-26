#include "minilisysm/interfaces/event_sink.hpp"
#include "minilisysm/interfaces/sched_delay_collector.hpp"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <vector>

#define CHECK(condition)                                                                            \
    do {                                                                                            \
        if (!(condition)) {                                                                         \
            std::cerr << "check failed: " #condition << " at line " << __LINE__ << "\n";          \
            return EXIT_FAILURE;                                                                    \
        }                                                                                           \
    } while (false)

namespace {

class FakeSink final : public lisysm::EventSink {
public:
    const char* name() const override { return "fake"; }

    lisysm::SpscRingBuffer<lisysm::InternalEvent>* add_input_queue(size_t capacity) override
    {
        queue = std::make_unique<lisysm::SpscRingBuffer<lisysm::InternalEvent>>(capacity);
        return queue.get();
    }

    bool start() override { return true; }
    void stop() override {}
    lisysm::SinkStats stats() const override { return stats_; }

    lisysm::SinkStats stats_;
    std::unique_ptr<lisysm::SpscRingBuffer<lisysm::InternalEvent>> queue;
};

class FakeSchedCollector final : public lisysm::SchedDelayCollectorInterface {
public:
    std::vector<lisysm::SchedDelaySample> collect() override { return {sample}; }
    uint64_t last_failure_count() const override { return failures; }
    lisysm::SchedDelayCollectorRuntimeStats runtime_stats() const override { return runtime; }

    lisysm::SchedDelaySample sample;
    lisysm::SchedDelayCollectorRuntimeStats runtime;
    uint64_t failures{0};
};

} // namespace

int main()
{
    FakeSink sink;
    CHECK(sink.name()[0] == 'f');
    CHECK(sink.add_input_queue(4) != nullptr);
    sink.stats_.accepted_events = 1;
    CHECK(sink.stats().accepted_events == 1);

    FakeSchedCollector collector;
    collector.sample.valid = true;
    collector.sample.delta_wait_sum_us = 10;
    collector.runtime.ebpf_ringbuf_drops = 2;
    collector.failures = 3;

    const std::vector<lisysm::SchedDelaySample> samples = collector.collect();
    CHECK(samples.size() == 1);
    CHECK(samples.front().valid);
    CHECK(samples.front().delta_wait_sum_us == 10);
    CHECK(collector.runtime_stats().ebpf_ringbuf_drops == 2);
    CHECK(collector.last_failure_count() == 3);
    return EXIT_SUCCESS;
}
