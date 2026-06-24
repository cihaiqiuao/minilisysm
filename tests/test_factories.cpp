#include "lisysm/collectors/collector_factory.hpp"
#include "lisysm/collectors/meminfo_collector.hpp"
#include "lisysm/collectors/sched_delay_collector.hpp"
#include "lisysm/collectors/self_status_collector.hpp"
#include "lisysm/core/config.hpp"
#include "lisysm/core/event.hpp"
#include "lisysm/queue/spsc_ring_buffer.hpp"
#include "lisysm/rules/rule_factory.hpp"
#include "lisysm/rules/rule_engine.hpp"
#include "lisysm/storage/event_store.hpp"
#include "lisysm/storage/storage_factory.hpp"

#include <cstdlib>
#include <iostream>
#include <vector>

#define CHECK(condition)                                                                            \
    do {                                                                                            \
        if (!(condition)) {                                                                         \
            std::cerr << "check failed: " #condition << " at line " << __LINE__ << "\n";          \
            return EXIT_FAILURE;                                                                    \
        }                                                                                           \
    } while (false)

int main()
{
    lisysm::MonitorConfig config;
    lisysm::SpscRingBuffer<lisysm::InternalEvent> fast_queue(config.event_queue_capacity);
    lisysm::SpscRingBuffer<lisysm::InternalEvent> sched_queue(config.event_queue_capacity);
    std::vector<lisysm::SpscRingBuffer<lisysm::InternalEvent>*> queues{&fast_queue, &sched_queue};

    auto meminfo = lisysm::CollectorFactory::create_meminfo_collector();
    auto self_status = lisysm::CollectorFactory::create_self_status_collector();
    auto sched_delay = lisysm::CollectorFactory::create_sched_delay_collector(config);
    auto fast_rules = lisysm::RuleFactory::create_fast_rule_engine(config);
    auto sched_rules = lisysm::RuleFactory::create_sched_rule_engine(config);
    auto store = lisysm::StorageFactory::create_event_store(config, queues);

    CHECK(meminfo != nullptr);
    CHECK(self_status != nullptr);
    CHECK(sched_delay != nullptr);
    CHECK(fast_rules != nullptr);
    CHECK(sched_rules != nullptr);
    CHECK(store != nullptr);
    return EXIT_SUCCESS;
}
