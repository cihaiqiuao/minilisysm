#include "minilisysm/collectors/collector_factory.hpp"
#include "minilisysm/collectors/cpu_usage_collector.hpp"
#include "minilisysm/collectors/io_delay_collector.hpp"
#include "minilisysm/collectors/meminfo_collector.hpp"
#include "minilisysm/collectors/self_status_collector.hpp"
#include "minilisysm/core/config.hpp"
#include "minilisysm/core/event.hpp"
#include "minilisysm/interfaces/event_sink.hpp"
#include "minilisysm/interfaces/sched_delay_collector.hpp"
#include "minilisysm/queue/spsc_ring_buffer.hpp"
#include "minilisysm/rules/rule_factory.hpp"
#include "minilisysm/rules/rule_engine.hpp"
#include "minilisysm/storage/storage_factory.hpp"

#include <cstdlib>
#include <iostream>
#include <vector>

#define CHECK(condition)                                                                                               \
    do {                                                                                                               \
        if (!(condition)) {                                                                                            \
            std::cerr << "check failed: " #condition << " at line " << __LINE__ << "\n";                               \
            return EXIT_FAILURE;                                                                                       \
        }                                                                                                              \
    } while (false)

int main() {
    lisysm::MonitorConfig config;
    lisysm::SpscRingBuffer<lisysm::InternalEvent> fast_queue(config.event_queue_capacity);
    lisysm::SpscRingBuffer<lisysm::InternalEvent> sched_queue(config.event_queue_capacity);
    std::vector<lisysm::SpscRingBuffer<lisysm::InternalEvent>*> queues{&fast_queue, &sched_queue};

    auto meminfo = lisysm::CollectorFactory::create_meminfo_collector();
    auto cpu_usage = lisysm::CollectorFactory::create_cpu_usage_collector(config);
    auto self_status = lisysm::CollectorFactory::create_self_status_collector();
    auto sched_delay = lisysm::CollectorFactory::create_sched_delay_collector(config);
    auto io_delay = lisysm::CollectorFactory::create_io_delay_collector(config);
    auto fast_rules = lisysm::RuleFactory::create_fast_rule_engine(config);
    auto sched_rules = lisysm::RuleFactory::create_sched_rule_engine(config);
    auto sinks = lisysm::StorageFactory::create_event_sinks(config);

    CHECK(meminfo != nullptr);
    CHECK(cpu_usage != nullptr);
    CHECK(self_status != nullptr);
    CHECK(sched_delay != nullptr);
    CHECK(io_delay != nullptr);
    CHECK(fast_rules != nullptr);
    CHECK(sched_rules != nullptr);
    CHECK(!sinks.empty());
    CHECK(sinks.front() != nullptr);
    return EXIT_SUCCESS;
}
