#include "minilisysm/collectors/io_delay_collector.hpp"
#include "minilisysm/core/config.hpp"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include <cmath>
#include <cstdint>

#define CHECK(condition)                                                                                               \
    do {                                                                                                               \
        if (!(condition)) {                                                                                            \
            std::cerr << "check failed: " #condition << " at line " << __LINE__ << "\n";                               \
            return EXIT_FAILURE;                                                                                       \
        }                                                                                                              \
    } while (false)

void write_file(const std::string& path, const std::string& content) {
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
    const std::string test_file = "/tmp/mock_diskstats";
    lisysm::MonitorConfig config;
    config.io_delay_enable = true;
    config.io_device_whitelist.clear();

    // Test 1: First collection (baselines established, no samples returned)
    write_file(test_file,
               "   8       0 sda 100 0 1000 500 200 0 2000 1000 0 1500 1500\n"
               " 259       0 nvme0n1 200 0 2000 1000 300 0 3000 1500 0 2500 2500\n"
               "   7       0 loop0 10 0 100 50 20 0 200 100 0 150 150\n"); // loop0 should be ignored

    lisysm::IoDelayCollector collector(config, test_file);
    std::vector<lisysm::IoDelaySample> samples1 = collector.collect();
    CHECK(samples1.empty()); // First run only establishes baseline

    // Wait a bit to ensure time delta > 0, we can't easily mock steady_clock without refactoring time.cpp
    // But time.cpp is outside the scope of this test. Let's just sleep 10ms.
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Test 2: Second collection (calculate deltas)
    // sda: +10 read_ios, +20 read_time, +10 write_ios, +20 write_time, +1 in_flight, +10 io_time
    // nvme0n1: +0 read_ios, +0 read_time, +0 write_ios, +0 write_time, +0 in_flight, +0 io_time (idle)
    write_file(test_file, "   8       0 sda 110 0 1100 520 210 0 2100 1020 1 1510 1530\n"
                          " 259       0 nvme0n1 200 0 2000 1000 300 0 3000 1500 0 2500 2500\n");

    std::vector<lisysm::IoDelaySample> samples2 = collector.collect();
    CHECK(samples2.size() == 1);

    bool found_sda = false;

    for (const auto& sample : samples2) {
        CHECK(sample.valid);
        if (sample.device == "sda") {
            found_sda = true;
            CHECK(sample.delta_io_count == 20); // 10 reads + 10 writes
            CHECK(sample.in_flight == 1);
            // avg_await = (20 + 20) / 20 = 2.0
            CHECK(std::abs(sample.avg_await_ms - 2.0) < 0.001);
            CHECK(sample.util_percent > 0.0); // time delta is > 0
        } else {
            CHECK(false); // Should not see nvme0n1 because delta_ios == 0
        }
    }
    CHECK(found_sda);

    // Test 3: Whitelist functionality
    lisysm::MonitorConfig whitelist_config;
    whitelist_config.io_delay_enable = true;
    whitelist_config.io_device_whitelist = {"sda"};
    lisysm::IoDelayCollector wl_collector(whitelist_config, test_file);

    // First run (baseline)
    CHECK(wl_collector.collect().empty());
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Write new values
    write_file(test_file, "   8       0 sda 120 0 1200 540 220 0 2200 1040 2 1520 1560\n"
                          " 259       0 nvme0n1 210 0 2100 1010 310 0 3100 1510 0 2510 2510\n");

    std::vector<lisysm::IoDelaySample> wl_samples = wl_collector.collect();
    CHECK(wl_samples.size() == 1);
    CHECK(wl_samples[0].device == "sda");

    // Test 4: File not found
    lisysm::IoDelayCollector not_found_collector(config, "/tmp/non_existent_mock_diskstats");
    CHECK(not_found_collector.collect().empty());
    CHECK(not_found_collector.last_failure_count() > 0);

    lisysm::MonitorConfig ttl_config = config;
    ttl_config.state_ttl_sec = 1;
    test_now_ms = 1'000;
    write_file(test_file, "   8       0 sda 100 0 1000 500 200 0 2000 1000 0 1500 1500\n"
                          " 259       0 nvme0n1 200 0 2000 1000 300 0 3000 1500 0 2500 2500\n");
    lisysm::IoDelayCollector ttl_collector(ttl_config, test_file, test_clock);
    CHECK(ttl_collector.collect().empty());

    test_now_ms = 2'500;
    write_file(test_file, "   8       0 sda 110 0 1100 520 210 0 2100 1020 1 1510 1530\n");
    CHECK(ttl_collector.collect().size() == 1);

    test_now_ms = 2'600;
    write_file(test_file, "   8       0 sda 120 0 1200 540 220 0 2200 1040 1 1520 1560\n"
                          " 259       0 nvme0n1 220 0 2200 1020 320 0 3200 1520 0 2520 2520\n");
    const std::vector<lisysm::IoDelaySample> ttl_samples = ttl_collector.collect();
    CHECK(ttl_samples.size() == 1);
    CHECK(ttl_samples[0].device == "sda");

    // Cleanup
    std::remove(test_file.c_str());

    return 0;
}
