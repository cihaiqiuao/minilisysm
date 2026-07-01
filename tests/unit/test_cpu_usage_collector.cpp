#include "minilisysm/collectors/cpu_usage_collector.hpp"
#include "minilisysm/core/config.hpp"

#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

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

int main() {
    const std::string test_file = "/tmp/mock_proc_stat";
    lisysm::MonitorConfig config;
    config.cpu_usage_enable = true;
    config.cpu_usage_mode = "both";

    write_file(test_file, "cpu  100 0 50 850 0 0 0 0 0 0\n"
                          "cpu0 50 0 25 425 0 0 0 0 0 0\n"
                          "cpu1 50 0 25 425 0 0 0 0 0 0\n");

    lisysm::CpuUsageCollector collector(config, test_file);
    CHECK(collector.collect().empty());

    write_file(test_file, "cpu  130 0 70 900 0 0 0 0 0 0\n"
                          "cpu0 65 0 35 450 0 0 0 0 0 0\n"
                          "cpu1 65 0 35 450 0 0 0 0 0 0\n");

    const std::vector<lisysm::CpuUsageSample> samples = collector.collect();
    CHECK(samples.size() == 3);
    bool found_total = false;
    bool found_cpu0 = false;
    bool found_cpu1 = false;
    for (const lisysm::CpuUsageSample& sample : samples) {
        CHECK(sample.valid);
        if (sample.cpu == "total") {
            found_total = true;
            CHECK(sample.delta_total_jiffies == 100);
            CHECK(sample.delta_idle_jiffies == 50);
            CHECK(std::abs(sample.usage_percent - 50.0) < 0.001);
        } else if (sample.cpu == "cpu0" || sample.cpu == "cpu1") {
            found_cpu0 = found_cpu0 || sample.cpu == "cpu0";
            found_cpu1 = found_cpu1 || sample.cpu == "cpu1";
            CHECK(sample.delta_total_jiffies == 50);
            CHECK(sample.delta_idle_jiffies == 25);
            CHECK(std::abs(sample.usage_percent - 50.0) < 0.001);
        } else {
            CHECK(false);
        }
    }
    CHECK(found_total);
    CHECK(found_cpu0);
    CHECK(found_cpu1);

    lisysm::MonitorConfig whitelist_config;
    whitelist_config.cpu_usage_enable = true;
    whitelist_config.cpu_usage_mode = "per_core";
    whitelist_config.cpu_usage_core_whitelist = {"cpu1"};
    lisysm::CpuUsageCollector whitelist_collector(whitelist_config, test_file);
    CHECK(whitelist_collector.collect().empty());
    write_file(test_file, "cpu  160 0 90 950 0 0 0 0 0 0\n"
                          "cpu0 80 0 45 475 0 0 0 0 0 0\n"
                          "cpu1 80 0 45 475 0 0 0 0 0 0\n");
    const std::vector<lisysm::CpuUsageSample> whitelist_samples = whitelist_collector.collect();
    CHECK(whitelist_samples.size() == 1);
    CHECK(whitelist_samples[0].cpu == "cpu1");

    lisysm::MonitorConfig disabled_config;
    disabled_config.cpu_usage_enable = false;
    lisysm::CpuUsageCollector disabled_collector(disabled_config, test_file);
    CHECK(disabled_collector.collect().empty());
    CHECK(disabled_collector.last_failure_count() == 0);

    lisysm::CpuUsageCollector missing_collector(config, "/tmp/non_existent_mock_proc_stat");
    CHECK(missing_collector.collect().empty());
    CHECK(missing_collector.last_failure_count() > 0);

    std::remove(test_file.c_str());
    return EXIT_SUCCESS;
}
