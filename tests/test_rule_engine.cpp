#include "lisysm/rule_engine.hpp"

#include <cstdlib>
#include <iostream>

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
    config.mem_available_warning_mb = 100;
    config.mem_available_critical_mb = 50;
    config.mem_available_recovery_mb = 150;
    config.continuous_warning_windows = 2;
    config.continuous_critical_windows = 2;
    config.recovery_windows = 2;

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
    return 0;
}
