#include "minilisysm/rules/rule_engine.hpp"

#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <string>

#define CHECK(condition)                                                                                               \
    do {                                                                                                               \
        if (!(condition)) {                                                                                            \
            std::cerr << "check failed: " #condition << " at line " << __LINE__ << "\n";                               \
            return EXIT_FAILURE;                                                                                       \
        }                                                                                                              \
    } while (false)

namespace {

uint64_t test_now_ms = 0;

uint64_t test_clock() {
    return test_now_ms;
}

} // namespace

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
    config.cpu_usage_warning_percent = 80.0;
    config.cpu_usage_critical_percent = 95.0;
    config.cpu_usage_recovery_percent = 60.0;
    config.cpu_usage_continuous_warning_windows = 1;
    config.cpu_usage_continuous_critical_windows = 1;
    config.cpu_usage_recovery_windows = 1;

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

    lisysm::CpuUsageSample cpu_sample;
    cpu_sample.valid = true;
    cpu_sample.cpu = "total";
    cpu_sample.usage_percent = 85.0;
    cpu_sample.delta_total_jiffies = 100;
    cpu_sample.delta_idle_jiffies = 15;
    auto cpu_warning = rules.evaluate_cpu_usage(cpu_sample);
    CHECK(cpu_warning.has_value());
    CHECK(cpu_warning->event_type == lisysm::EventType::CpuUsageRisk);
    CHECK(cpu_warning->level == lisysm::EventLevel::Warning);

    cpu_sample.usage_percent = 98.0;
    cpu_sample.delta_idle_jiffies = 2;
    auto cpu_critical = rules.evaluate_cpu_usage(cpu_sample);
    CHECK(cpu_critical.has_value());
    CHECK(cpu_critical->level == lisysm::EventLevel::Critical);

    cpu_sample.usage_percent = 40.0;
    cpu_sample.delta_idle_jiffies = 60;
    auto cpu_recovery = rules.evaluate_cpu_usage(cpu_sample);
    CHECK(cpu_recovery.has_value());
    CHECK(cpu_recovery->level == lisysm::EventLevel::Recovery);

    lisysm::CpuUsageSample core_sample;
    core_sample.valid = true;
    core_sample.cpu = "cpu2";
    core_sample.usage_percent = 98.0;
    core_sample.delta_total_jiffies = 100;
    core_sample.delta_idle_jiffies = 2;
    auto core_critical = rules.evaluate_cpu_usage(core_sample);
    CHECK(core_critical.has_value());
    CHECK(core_critical->target.data() == std::string("cpu2"));
    CHECK(core_critical->level == lisysm::EventLevel::Critical);

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

    lisysm::MonitorConfig process_memory_config = config;
    process_memory_config.process_memory_growth_warning_mb = 100;
    process_memory_config.process_memory_growth_critical_mb = 200;
    process_memory_config.process_memory_growth_recovery_mb = 20;
    process_memory_config.process_memory_growth_window_sec = 600;
    lisysm::RuleEngine process_memory_rules(process_memory_config);
    auto process_memory_warning = process_memory_rules.evaluate_process_memory_growth("train_stability", 42, 120.0);
    CHECK(process_memory_warning.has_value());
    CHECK(process_memory_warning->event_type == lisysm::EventType::WhitelistedProcessMemoryRisk);
    CHECK(process_memory_warning->level == lisysm::EventLevel::Warning);
    auto process_memory_critical = process_memory_rules.evaluate_process_memory_growth("train_stability", 42, 220.0);
    CHECK(process_memory_critical.has_value());
    CHECK(process_memory_critical->level == lisysm::EventLevel::Critical);
    auto process_memory_recovery = process_memory_rules.evaluate_process_memory_growth("train_stability", 42, 10.0);
    CHECK(process_memory_recovery.has_value());
    CHECK(process_memory_recovery->level == lisysm::EventLevel::Recovery);
    lisysm::MonitorConfig window_config = config;
    window_config.fast_collect_interval_ms = 2'000;
    window_config.low_freq_collect_interval_ms = 7'000;
    window_config.cooldown_sec = 0;
    window_config.continuous_warning_windows = 1;
    window_config.continuous_critical_windows = 2;
    window_config.recovery_windows = 3;
    window_config.self_recovery_windows = 3;
    window_config.cpu_usage_continuous_warning_windows = 2;
    window_config.cpu_usage_continuous_critical_windows = 3;
    window_config.cpu_usage_recovery_windows = 4;
    window_config.sched_continuous_warning_windows = 2;
    window_config.sched_continuous_critical_windows = 3;
    window_config.sched_recovery_windows = 4;
    window_config.io_continuous_warning_windows = 2;
    window_config.io_continuous_critical_windows = 3;
    window_config.io_recovery_windows = 4;
    lisysm::RuleEngine window_rules(window_config);

    lisysm::MeminfoSample window_memory_sample;
    window_memory_sample.valid = true;
    window_memory_sample.mem_total_kb = 1024 * 1024;
    window_memory_sample.mem_available_kb = 90 * 1024;
    auto window_memory_warning = window_rules.evaluate_memory(window_memory_sample);
    CHECK(window_memory_warning.has_value());
    CHECK(window_memory_warning->window_sec == 2);
    window_memory_sample.mem_available_kb = 40 * 1024;
    CHECK(!window_rules.evaluate_memory(window_memory_sample).has_value());
    auto window_memory_critical = window_rules.evaluate_memory(window_memory_sample);
    CHECK(window_memory_critical.has_value());
    CHECK(window_memory_critical->window_sec == 4);
    window_memory_sample.mem_available_kb = 200 * 1024;
    CHECK(!window_rules.evaluate_memory(window_memory_sample).has_value());
    CHECK(!window_rules.evaluate_memory(window_memory_sample).has_value());
    auto window_memory_recovery = window_rules.evaluate_memory(window_memory_sample);
    CHECK(window_memory_recovery.has_value());
    CHECK(window_memory_recovery->window_sec == 6);

    auto window_self_warning = window_rules.evaluate_self_rss(15 * 1024);
    CHECK(window_self_warning.has_value());
    CHECK(window_self_warning->window_sec == 2);
    CHECK(!window_rules.evaluate_self_rss(21 * 1024).has_value());
    auto window_self_critical = window_rules.evaluate_self_rss(21 * 1024);
    CHECK(window_self_critical.has_value());
    CHECK(window_self_critical->window_sec == 4);
    CHECK(!window_rules.evaluate_self_rss(4 * 1024).has_value());
    CHECK(!window_rules.evaluate_self_rss(4 * 1024).has_value());
    auto window_self_recovery = window_rules.evaluate_self_rss(4 * 1024);
    CHECK(window_self_recovery.has_value());
    CHECK(window_self_recovery->window_sec == 6);

    lisysm::QueueSnapshot window_queue_sample;
    window_queue_sample.capacity = 10;
    window_queue_sample.depth = 8;
    auto window_queue_warning = window_rules.evaluate_queue(window_queue_sample);
    CHECK(window_queue_warning.has_value());
    CHECK(window_queue_warning->window_sec == 2);
    window_queue_sample.depth = 10;
    CHECK(!window_rules.evaluate_queue(window_queue_sample).has_value());
    auto window_queue_critical = window_rules.evaluate_queue(window_queue_sample);
    CHECK(window_queue_critical.has_value());
    CHECK(window_queue_critical->window_sec == 4);
    window_queue_sample.depth = 1;
    CHECK(!window_rules.evaluate_queue(window_queue_sample).has_value());
    CHECK(!window_rules.evaluate_queue(window_queue_sample).has_value());
    auto window_queue_recovery = window_rules.evaluate_queue(window_queue_sample);
    CHECK(window_queue_recovery.has_value());
    CHECK(window_queue_recovery->window_sec == 6);

    lisysm::CpuUsageSample window_cpu_sample;
    window_cpu_sample.valid = true;
    window_cpu_sample.cpu = "total";
    window_cpu_sample.usage_percent = 85.0;
    CHECK(!window_rules.evaluate_cpu_usage(window_cpu_sample).has_value());
    auto window_cpu_warning = window_rules.evaluate_cpu_usage(window_cpu_sample);
    CHECK(window_cpu_warning.has_value());
    CHECK(window_cpu_warning->window_sec == 4);
    window_cpu_sample.usage_percent = 98.0;
    CHECK(!window_rules.evaluate_cpu_usage(window_cpu_sample).has_value());
    CHECK(!window_rules.evaluate_cpu_usage(window_cpu_sample).has_value());
    auto window_cpu_critical = window_rules.evaluate_cpu_usage(window_cpu_sample);
    CHECK(window_cpu_critical.has_value());
    CHECK(window_cpu_critical->window_sec == 6);
    window_cpu_sample.usage_percent = 40.0;
    CHECK(!window_rules.evaluate_cpu_usage(window_cpu_sample).has_value());
    CHECK(!window_rules.evaluate_cpu_usage(window_cpu_sample).has_value());
    CHECK(!window_rules.evaluate_cpu_usage(window_cpu_sample).has_value());
    auto window_cpu_recovery = window_rules.evaluate_cpu_usage(window_cpu_sample);
    CHECK(window_cpu_recovery.has_value());
    CHECK(window_cpu_recovery->window_sec == 8);

    lisysm::SchedDelaySample window_sched_sample;
    window_sched_sample.valid = true;
    window_sched_sample.pid = 21;
    window_sched_sample.tid = 22;
    window_sched_sample.delta_wait_sum_us = 200;
    window_sched_sample.delta_involuntary_switches = 2;
    CHECK(!window_rules.evaluate_sched_delay(window_sched_sample).has_value());
    auto window_sched_warning = window_rules.evaluate_sched_delay(window_sched_sample);
    CHECK(window_sched_warning.has_value());
    CHECK(window_sched_warning->window_sec == 14);
    window_sched_sample.delta_wait_sum_us = 400;
    CHECK(!window_rules.evaluate_sched_delay(window_sched_sample).has_value());
    CHECK(!window_rules.evaluate_sched_delay(window_sched_sample).has_value());
    auto window_sched_critical = window_rules.evaluate_sched_delay(window_sched_sample);
    CHECK(window_sched_critical.has_value());
    CHECK(window_sched_critical->window_sec == 21);
    window_sched_sample.delta_wait_sum_us = 10;
    window_sched_sample.delta_involuntary_switches = 0;
    CHECK(!window_rules.evaluate_sched_delay(window_sched_sample).has_value());
    CHECK(!window_rules.evaluate_sched_delay(window_sched_sample).has_value());
    CHECK(!window_rules.evaluate_sched_delay(window_sched_sample).has_value());
    auto window_sched_recovery = window_rules.evaluate_sched_delay(window_sched_sample);
    CHECK(window_sched_recovery.has_value());
    CHECK(window_sched_recovery->window_sec == 28);

    lisysm::IoDelaySample window_io_sample;
    window_io_sample.valid = true;
    window_io_sample.device = "sdb";
    window_io_sample.avg_await_ms = 15.0;
    CHECK(!window_rules.evaluate_io_delay(window_io_sample).has_value());
    auto window_io_warning = window_rules.evaluate_io_delay(window_io_sample);
    CHECK(window_io_warning.has_value());
    CHECK(window_io_warning->window_sec == 14);
    window_io_sample.avg_await_ms = 40.0;
    CHECK(!window_rules.evaluate_io_delay(window_io_sample).has_value());
    CHECK(!window_rules.evaluate_io_delay(window_io_sample).has_value());
    auto window_io_critical = window_rules.evaluate_io_delay(window_io_sample);
    CHECK(window_io_critical.has_value());
    CHECK(window_io_critical->window_sec == 21);
    window_io_sample.avg_await_ms = 1.0;
    CHECK(!window_rules.evaluate_io_delay(window_io_sample).has_value());
    CHECK(!window_rules.evaluate_io_delay(window_io_sample).has_value());
    CHECK(!window_rules.evaluate_io_delay(window_io_sample).has_value());
    auto window_io_recovery = window_rules.evaluate_io_delay(window_io_sample);
    CHECK(window_io_recovery.has_value());
    CHECK(window_io_recovery->window_sec == 28);

    lisysm::MonitorConfig runtime_config = config;
    runtime_config.cooldown_sec = 60;
    runtime_config.state_ttl_sec = 1;
    runtime_config.continuous_warning_windows = 1;
    runtime_config.continuous_critical_windows = 1;
    runtime_config.recovery_windows = 1;
    runtime_config.cpu_usage_continuous_warning_windows = 1;
    runtime_config.cpu_usage_continuous_critical_windows = 1;
    runtime_config.cpu_usage_recovery_windows = 1;
    lisysm::RuleEngine runtime_rules(runtime_config, test_clock);
    lisysm::MeminfoSample runtime_sample;
    runtime_sample.valid = true;
    runtime_sample.mem_total_kb = 1024 * 1024;

    test_now_ms = 1'000;
    runtime_sample.mem_available_kb = 90 * 1024;
    auto first_warning = runtime_rules.evaluate_memory(runtime_sample);
    CHECK(first_warning.has_value());
    CHECK(first_warning->level == lisysm::EventLevel::Warning);

    test_now_ms += 1'000;
    runtime_sample.mem_available_kb = 200 * 1024;
    auto first_recovery = runtime_rules.evaluate_memory(runtime_sample);
    CHECK(first_recovery.has_value());
    CHECK(first_recovery->level == lisysm::EventLevel::Recovery);

    test_now_ms += 1'000;
    runtime_sample.mem_available_kb = 90 * 1024;
    CHECK(!runtime_rules.evaluate_memory(runtime_sample).has_value());

    test_now_ms += 1'000;
    runtime_sample.mem_available_kb = 200 * 1024;
    CHECK(!runtime_rules.evaluate_memory(runtime_sample).has_value());

    test_now_ms += 1'000;
    runtime_sample.mem_available_kb = 40 * 1024;
    auto cooldown_critical = runtime_rules.evaluate_memory(runtime_sample);
    CHECK(cooldown_critical.has_value());
    CHECK(cooldown_critical->level == lisysm::EventLevel::Critical);

    test_now_ms += 1'000;
    runtime_sample.mem_available_kb = 200 * 1024;
    auto critical_recovery = runtime_rules.evaluate_memory(runtime_sample);
    CHECK(critical_recovery.has_value());
    CHECK(critical_recovery->level == lisysm::EventLevel::Recovery);

    test_now_ms = 66'000;
    runtime_sample.mem_available_kb = 90 * 1024;
    auto warning_after_cooldown = runtime_rules.evaluate_memory(runtime_sample);
    CHECK(warning_after_cooldown.has_value());
    CHECK(warning_after_cooldown->level == lisysm::EventLevel::Warning);

    lisysm::CpuUsageSample cpu_zero;
    cpu_zero.valid = true;
    cpu_zero.cpu = "cpu0";
    cpu_zero.usage_percent = 90.0;
    lisysm::CpuUsageSample cpu_one = cpu_zero;
    cpu_one.cpu = "cpu1";
    test_now_ms = 70'000;
    CHECK(runtime_rules.evaluate_cpu_usage(cpu_zero).has_value());
    CHECK(runtime_rules.evaluate_cpu_usage(cpu_one).has_value());

    test_now_ms = 80'000;
    // The current target must keep its active context even after one TTL; pruning it would emit a duplicate warning.
    CHECK(!runtime_rules.evaluate_cpu_usage(cpu_one).has_value());
    test_now_ms = 82'000;
    // cpu0 was absent while cpu1 was evaluated, so its expired context must be rebuilt on return.
    CHECK(runtime_rules.evaluate_cpu_usage(cpu_zero).has_value());

    lisysm::MonitorConfig ttl_rule_config = config;
    ttl_rule_config.cooldown_sec = 0;
    ttl_rule_config.state_ttl_sec = 1;
    ttl_rule_config.sched_continuous_warning_windows = 2;
    ttl_rule_config.io_continuous_warning_windows = 2;

    lisysm::RuleEngine sched_ttl_rules(ttl_rule_config, test_clock);
    lisysm::SchedDelaySample sched_current;
    sched_current.valid = true;
    sched_current.pid = 31;
    sched_current.tid = 32;
    sched_current.delta_wait_sum_us = 200;
    sched_current.delta_involuntary_switches = 2;
    lisysm::SchedDelaySample sched_missing = sched_current;
    sched_missing.pid = 41;
    sched_missing.tid = 42;

    test_now_ms = 90'000;
    CHECK(!sched_ttl_rules.evaluate_sched_delay(sched_current).has_value());
    CHECK(!sched_ttl_rules.evaluate_sched_delay(sched_missing).has_value());
    test_now_ms = 92'000;
    auto sched_current_warning = sched_ttl_rules.evaluate_sched_delay(sched_current);
    CHECK(sched_current_warning.has_value());
    CHECK(sched_current_warning->level == lisysm::EventLevel::Warning);
    test_now_ms = 94'000;
    CHECK(!sched_ttl_rules.evaluate_sched_delay(sched_missing).has_value());

    lisysm::RuleEngine io_ttl_rules(ttl_rule_config, test_clock);
    lisysm::IoDelaySample io_current;
    io_current.valid = true;
    io_current.device = "sda";
    io_current.avg_await_ms = 15.0;
    lisysm::IoDelaySample io_missing = io_current;
    io_missing.device = "sdb";

    test_now_ms = 100'000;
    CHECK(!io_ttl_rules.evaluate_io_delay(io_current).has_value());
    CHECK(!io_ttl_rules.evaluate_io_delay(io_missing).has_value());
    test_now_ms = 102'000;
    auto io_current_warning = io_ttl_rules.evaluate_io_delay(io_current);
    CHECK(io_current_warning.has_value());
    CHECK(io_current_warning->level == lisysm::EventLevel::Warning);
    test_now_ms = 104'000;
    CHECK(!io_ttl_rules.evaluate_io_delay(io_missing).has_value());

    lisysm::MonitorConfig process_ttl_config = config;
    process_ttl_config.cooldown_sec = 0;
    process_ttl_config.state_ttl_sec = 1;
    process_ttl_config.process_memory_enable = true;
    process_ttl_config.process_memory_growth_warning_mb = 10;
    process_ttl_config.process_memory_growth_critical_mb = 100;
    process_ttl_config.process_memory_growth_recovery_mb = 0;
    lisysm::RuleEngine process_ttl_rules(process_ttl_config, test_clock);

    test_now_ms = 110'000;
    auto first_process_warning = process_ttl_rules.evaluate_process_memory_growth("train_stability", 51, 20.0);
    CHECK(first_process_warning.has_value());
    CHECK(first_process_warning->level == lisysm::EventLevel::Warning);

    test_now_ms = 112'000;
    auto other_process_warning = process_ttl_rules.evaluate_process_memory_growth("train_stability", 52, 20.0);
    CHECK(other_process_warning.has_value());

    test_now_ms = 114'000;
    CHECK(!process_ttl_rules.evaluate_process_memory_growth("train_stability", 51, 0.0).has_value());

    lisysm::MonitorConfig reused_pid_config = process_ttl_config;
    reused_pid_config.cooldown_sec = 60;
    lisysm::RuleEngine reused_pid_rules(reused_pid_config, test_clock);
    test_now_ms = 120'000;
    auto old_instance = reused_pid_rules.evaluate_process_memory_growth("train_stability", 61, 20.0);
    CHECK(old_instance.has_value());
    CHECK(old_instance->level == lisysm::EventLevel::Warning);

    reused_pid_rules.forget_process_memory("train_stability", 61);
    test_now_ms = 121'000;
    auto new_instance = reused_pid_rules.evaluate_process_memory_growth("train_stability", 61, 20.0);
    CHECK(new_instance.has_value());
    CHECK(new_instance->level == lisysm::EventLevel::Warning);
    CHECK(new_instance->hit_count == 1);
    return 0;
}
