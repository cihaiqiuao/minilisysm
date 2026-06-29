#include "minilisysm/rules/rule_engine.hpp"

#include <cstdlib>
#include <iostream>

#define CHECK(condition)                                                                                               \
    do {                                                                                                               \
        if (!(condition)) {                                                                                            \
            std::cerr << "check failed: " #condition << " at line " << __LINE__ << "\n";                               \
            return EXIT_FAILURE;                                                                                       \
        }                                                                                                              \
    } while (false)

int main() {
    lisysm::MonitorConfig config;
    config.mem_available_warning_mb = 100;
    config.mem_available_critical_mb = 50;
    config.mem_available_recovery_mb = 150;
    config.continuous_warning_windows = 2;
    config.continuous_critical_windows = 2;
    config.recovery_windows = 2;
    config.queue_warning_percent = 70;
    config.queue_critical_percent = 90;
    config.queue_recovery_percent = 50;
    config.self_recovery_windows = 1;
    config.self_rss_soft_limit_mb = 10;
    config.self_rss_hard_limit_mb = 20;
    config.self_rss_recovery_mb = 5;
    config.sched_wait_sum_warning_us = 100;
    config.sched_wait_sum_critical_us = 300;
    config.sched_wait_sum_recovery_us = 50;
    config.sched_involuntary_switch_warning = 1;
    config.sched_continuous_warning_windows = 1;
    config.sched_continuous_critical_windows = 1;
    config.sched_recovery_windows = 1;
    config.io_await_warning_ms = 10.0;
    config.io_await_critical_ms = 30.0;
    config.io_await_recovery_ms = 5.0;
    config.io_util_warning_percent = 80.0;
    config.io_util_critical_percent = 95.0;
    config.io_util_recovery_percent = 50.0;
    config.io_continuous_warning_windows = 1;
    config.io_continuous_critical_windows = 1;
    config.io_recovery_windows = 1;

    lisysm::RuleEngine rules(config);
    lisysm::MeminfoSample sample;
    sample.valid = true;
    sample.mem_total_kb = 1024 * 1024;
    sample.mem_available_kb = 90 * 1024;

    CHECK(!rules.evaluate_memory(sample).has_value());
    auto warning = rules.evaluate_memory(sample);
    CHECK(warning.has_value());
    CHECK(warning->level == lisysm::EventLevel::Warning);

    sample.mem_available_kb = 40 * 1024;
    CHECK(!rules.evaluate_memory(sample).has_value());
    auto critical = rules.evaluate_memory(sample);
    CHECK(critical.has_value());
    CHECK(critical->level == lisysm::EventLevel::Critical);

    sample.mem_available_kb = 200 * 1024;
    CHECK(!rules.evaluate_memory(sample).has_value());
    auto recovery = rules.evaluate_memory(sample);
    CHECK(recovery.has_value());
    CHECK(recovery->level == lisysm::EventLevel::Recovery);

    CHECK(!rules.evaluate_self_rss(21 * 1024).has_value());
    auto self_critical = rules.evaluate_self_rss(21 * 1024);
    CHECK(self_critical.has_value());
    CHECK(self_critical->event_type == lisysm::EventType::MonitorMemoryPressure);
    CHECK(self_critical->level == lisysm::EventLevel::Critical);

    auto self_recovery = rules.evaluate_self_rss(4 * 1024);
    CHECK(self_recovery.has_value());
    CHECK(self_recovery->level == lisysm::EventLevel::Recovery);

    lisysm::QueueSnapshot queue_snapshot;
    queue_snapshot.depth = 8;
    queue_snapshot.capacity = 10;
    CHECK(!rules.evaluate_queue(queue_snapshot).has_value());
    auto queue_event = rules.evaluate_queue(queue_snapshot);
    CHECK(queue_event.has_value());
    CHECK(queue_event->event_type == lisysm::EventType::QueuePressure);
    CHECK(queue_event->level == lisysm::EventLevel::Warning);

    queue_snapshot.depth = 1;
    auto queue_recovery = rules.evaluate_queue(queue_snapshot);
    CHECK(queue_recovery.has_value());
    CHECK(queue_recovery->level == lisysm::EventLevel::Recovery);

    lisysm::MonitorConfig queue_config = config;
    queue_config.continuous_warning_windows = 1;
    queue_config.continuous_critical_windows = 1;
    lisysm::RuleEngine queue_rules(queue_config);
    lisysm::QueueSnapshot sink_snapshot;
    sink_snapshot.sink_depth = 8;
    sink_snapshot.sink_capacity = 10;
    auto sink_queue_event = queue_rules.evaluate_queue(sink_snapshot);
    CHECK(sink_queue_event.has_value());
    CHECK(sink_queue_event->level == lisysm::EventLevel::Warning);
    CHECK(sink_queue_event->evidence_count == 6);

    lisysm::RuleEngine dispatcher_failure_rules(queue_config);
    lisysm::QueueSnapshot dispatcher_failure_snapshot;
    dispatcher_failure_snapshot.capacity = 10;
    dispatcher_failure_snapshot.dispatcher_sink_push_failures = 1;
    auto dispatcher_failure_event = dispatcher_failure_rules.evaluate_queue(dispatcher_failure_snapshot);
    CHECK(dispatcher_failure_event.has_value());
    CHECK(dispatcher_failure_event->level == lisysm::EventLevel::Warning);

    lisysm::RuleEngine critical_queue_rules(queue_config);
    lisysm::QueueSnapshot critical_drop_snapshot;
    critical_drop_snapshot.capacity = 10;
    critical_drop_snapshot.dropped_critical_count = 1;
    auto critical_queue_event = critical_queue_rules.evaluate_queue(critical_drop_snapshot);
    CHECK(critical_queue_event.has_value());
    CHECK(critical_queue_event->level == lisysm::EventLevel::Critical);

    lisysm::SchedDelaySample sched_sample;
    sched_sample.valid = true;
    sched_sample.pid = 11;
    sched_sample.tid = 12;
    sched_sample.delta_wait_sum_us = 200;
    sched_sample.delta_involuntary_switches = 2;
    auto sched_warning = rules.evaluate_sched_delay(sched_sample);
    CHECK(sched_warning.has_value());
    CHECK(sched_warning->event_type == lisysm::EventType::SchedDelayRisk);
    CHECK(sched_warning->level == lisysm::EventLevel::Warning);

    sched_sample.delta_wait_sum_us = 400;
    auto sched_critical = rules.evaluate_sched_delay(sched_sample);
    CHECK(sched_critical.has_value());
    CHECK(sched_critical->level == lisysm::EventLevel::Critical);

    sched_sample.delta_wait_sum_us = 10;
    sched_sample.delta_involuntary_switches = 0;
    auto sched_recovery = rules.evaluate_sched_delay(sched_sample);
    CHECK(sched_recovery.has_value());
    CHECK(sched_recovery->level == lisysm::EventLevel::Recovery);

    lisysm::IoDelaySample io_sample;
    io_sample.valid = true;
    io_sample.device = "sda";
    io_sample.delta_io_count = 10;
    io_sample.avg_await_ms = 15.0;
    io_sample.util_percent = 10.0;
    auto io_warning = rules.evaluate_io_delay(io_sample);
    CHECK(io_warning.has_value());
    CHECK(io_warning->event_type == lisysm::EventType::IoDelayRisk);
    CHECK(io_warning->level == lisysm::EventLevel::Warning);

    io_sample.avg_await_ms = 40.0;
    auto io_critical = rules.evaluate_io_delay(io_sample);
    CHECK(io_critical.has_value());
    CHECK(io_critical->level == lisysm::EventLevel::Critical);

    io_sample.avg_await_ms = 1.0;
    io_sample.util_percent = 1.0;
    auto io_recovery = rules.evaluate_io_delay(io_sample);
    CHECK(io_recovery.has_value());
    CHECK(io_recovery->level == lisysm::EventLevel::Recovery);
    return 0;
}
