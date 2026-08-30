#include "minilisysm/collectors/sched_delay_collector.hpp"
#include "minilisysm/core/config.hpp"

#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

#define CHECK(condition)                                                                                               \
    do {                                                                                                               \
        if (!(condition)) {                                                                                            \
            std::cerr << "check failed: " #condition << " at line " << __LINE__ << "\n";                               \
            return EXIT_FAILURE;                                                                                       \
        }                                                                                                              \
    } while (false)

#define CHECK_EQ(actual, expected)                                                                                     \
    do {                                                                                                               \
        if ((actual) != (expected)) {                                                                                  \
            std::cerr << "check failed: " #actual " == " #expected " at line " << __LINE__                             \
                      << "\n  actual: " << (actual) << "\n  expected: " << (expected) << "\n";                         \
            return EXIT_FAILURE;                                                                                       \
        }                                                                                                              \
    } while (false)

void write_file(const fs::path& path, const std::string& content) {
    fs::create_directories(path.parent_path());
    std::ofstream os(path);
    os << content;
}

namespace {

uint64_t test_now_ms = 0;

uint64_t test_clock() {
    return test_now_ms;
}

} // namespace

int main() {
    const fs::path mock_proc = "/tmp/mock_proc_sched";
    fs::remove_all(mock_proc);
    fs::create_directories(mock_proc);

    // Setup mock /proc structure
    // /tmp/mock_proc_sched/100/comm -> "target_proc"
    // /tmp/mock_proc_sched/100/task/100/comm -> "target_proc"
    // /tmp/mock_proc_sched/100/task/100/sched -> "se.statistics.wait_sum: 1000\nnrvcs: 5"
    // /tmp/mock_proc_sched/100/task/101/comm -> "target_thread"
    // /tmp/mock_proc_sched/100/task/101/sched -> "se.statistics.wait_sum: 2000\nnrvcs: 10"

    // /tmp/mock_proc_sched/200/comm -> "ignored_proc"
    // /tmp/mock_proc_sched/200/task/200/comm -> "ignored_proc"
    // /tmp/mock_proc_sched/200/task/200/sched -> "se.statistics.wait_sum: 500\nnrvcs: 1"

    write_file(mock_proc / "100" / "comm", "target_proc\n");
    write_file(mock_proc / "100" / "task" / "100" / "comm", "target_proc\n");
    write_file(mock_proc / "100" / "task" / "100" / "sched",
               "cpu_clock                                    :             6188.083398\n"
               "se.statistics.wait_sum                       :             1000.000000\n"
               "nvcsw                                        :                  100057\n"
               "nr_involuntary_switches                      :                       5\n");

    write_file(mock_proc / "100" / "task" / "101" / "comm", "target_thread\n");
    write_file(mock_proc / "100" / "task" / "101" / "sched",
               "se.statistics.wait_sum                       :             2000.000000\n"
               "nr_involuntary_switches                      :                      10\n");

    write_file(mock_proc / "200" / "comm", "ignored_proc\n");
    write_file(mock_proc / "200" / "task" / "200" / "comm", "ignored_proc\n");
    write_file(mock_proc / "200" / "task" / "200" / "sched",
               "se.statistics.wait_sum                       :              500.000000\n"
               "nr_involuntary_switches                      :                       1\n");

    lisysm::MonitorConfig config;
    config.sched_delay_enable = true;
    config.sched_process_whitelist = {"target_proc"}; // Only scan target_proc

    lisysm::SchedDelayCollector collector(config, mock_proc.string());

    // Test 1: First collection (baselines established)
    std::vector<lisysm::SchedDelaySample> samples1 = collector.collect();
    CHECK(samples1.empty()); // No samples on first run

    // Update mock files to simulate time passing and scheduling
    // thread 100: +500 wait_sum (1500), +2 nivcsw (7)
    // thread 101: +1000 wait_sum (3000), +5 nivcsw (15)
    write_file(mock_proc / "100" / "task" / "100" / "sched",
               "se.statistics.wait_sum                       :             1500.000000\n"
               "nr_involuntary_switches                      :                       7\n");

    write_file(mock_proc / "100" / "task" / "101" / "sched",
               "se.statistics.wait_sum                       :             3000.000000\n"
               "nr_involuntary_switches                      :                      15\n");

    // Test 2: Second collection
    std::vector<lisysm::SchedDelaySample> samples2 = collector.collect();
    CHECK(samples2.size() == 2); // 100 and 101

    bool found_100 = false;
    bool found_101 = false;
    for (const auto& sample : samples2) {
        std::cerr << "Sample pid=" << sample.pid << " tid=" << sample.tid << " wait=" << sample.delta_wait_sum_us
                  << "\n";
        CHECK(sample.valid);
        CHECK(sample.pid == 100);
        if (sample.tid == 100) {
            found_100 = true;
            CHECK_EQ(sample.delta_wait_sum_us, 500000ULL);     // 1500 - 1000 = 500 ms = 500,000 us
            CHECK_EQ(sample.delta_involuntary_switches, 2ULL); // 7 - 5 = 2
        } else if (sample.tid == 101) {
            found_101 = true;
            CHECK_EQ(sample.delta_wait_sum_us, 1000000ULL);    // 3000 - 2000 = 1000 ms = 1,000,000 us
            CHECK_EQ(sample.delta_involuntary_switches, 5ULL); // 15 - 10 = 5
        } else {
            CHECK(false); // unexpected tid
        }
    }
    CHECK(found_100 && found_101);

    // Test 3: Thread filtering
    lisysm::MonitorConfig thread_config;
    thread_config.sched_delay_enable = true;
    thread_config.sched_process_whitelist = {"target_proc"};
    thread_config.sched_thread_whitelist = {"target_thread"}; // Only thread 101

    lisysm::SchedDelayCollector thread_collector(thread_config, mock_proc.string());
    CHECK(thread_collector.collect().empty()); // Baseline

    write_file(mock_proc / "100" / "task" / "100" / "sched",
               "se.statistics.wait_sum : 2000.0\nnr_involuntary_switches : 10\n");
    write_file(mock_proc / "100" / "task" / "101" / "sched",
               "se.statistics.wait_sum : 4000.0\nnr_involuntary_switches : 20\n");

    std::vector<lisysm::SchedDelaySample> thread_samples = thread_collector.collect();
    CHECK(thread_samples.size() == 1);
    CHECK(thread_samples[0].tid == 101);
    CHECK_EQ(thread_samples[0].delta_wait_sum_us, 1000000ULL);    // 4000 - 3000 = 1000 ms = 1,000,000 us
    CHECK_EQ(thread_samples[0].delta_involuntary_switches, 5ULL); // 20 - 15 = 5

    // Test 4: Missing /proc directory
    lisysm::SchedDelayCollector not_found_collector(config, "/tmp/non_existent_mock_proc");
    CHECK(not_found_collector.collect().empty());
    CHECK(not_found_collector.last_failure_count() > 0);

    lisysm::MonitorConfig ttl_config = config;
    ttl_config.state_ttl_sec = 1;
    ttl_config.sched_thread_whitelist = {"target_thread"};
    test_now_ms = 1'000;
    write_file(mock_proc / "100" / "task" / "101" / "comm", "target_thread\n");
    write_file(mock_proc / "100" / "task" / "101" / "sched",
               "se.statistics.wait_sum : 5000.0\nnr_involuntary_switches : 25\n");
    lisysm::SchedDelayCollector ttl_collector(ttl_config, mock_proc.string(), test_clock);
    CHECK(ttl_collector.collect().empty());

    test_now_ms = 2'500;
    fs::remove_all(mock_proc / "100" / "task" / "101");
    CHECK(ttl_collector.collect().empty());

    test_now_ms = 2'600;
    write_file(mock_proc / "100" / "task" / "101" / "comm", "renamed_thread\n");
    write_file(mock_proc / "100" / "task" / "101" / "sched",
               "se.statistics.wait_sum : 6000.0\nnr_involuntary_switches : 30\n");
    CHECK(ttl_collector.collect().empty());

    // Cleanup
    fs::remove_all(mock_proc);

    return 0;
}
