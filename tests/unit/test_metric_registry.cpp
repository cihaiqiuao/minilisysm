#include "minilisysm/runtime/metric_registry.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

#define CHECK(condition)                                                                                               \
    do {                                                                                                               \
        if (!(condition)) {                                                                                            \
            std::cerr << "check failed: " #condition << " at line " << __LINE__ << "\n";                               \
            return EXIT_FAILURE;                                                                                       \
        }                                                                                                              \
    } while (false)

int main() {
    lisysm::MetricRegistry registry;
    registry.set_gauge("minilisysm_test_gauge", 7.0, {{"device", "sda"}});
    registry.inc_counter("minilisysm_test_total", 1.0, {{"type", "a\"b"}});
    registry.inc_counter("minilisysm_test_total", 2.0, {{"type", "a\"b"}});

    const std::string text = registry.render_prometheus();
    CHECK(text.find("# TYPE minilisysm_test_gauge gauge") != std::string::npos);
    CHECK(text.find("minilisysm_test_gauge{device=\"sda\"} 7") != std::string::npos);
    CHECK(text.find("# TYPE minilisysm_test_total counter") != std::string::npos);
    CHECK(text.find("minilisysm_test_total{type=\"a\\\"b\"} 3") != std::string::npos);

    registry.set_gauge_family("minilisysm_sched_wait_us",
                              {{10.0, {{"pid", "1"}, {"tid", "1"}}}, {20.0, {{"pid", "2"}, {"tid", "2"}}}});
    registry.set_gauge_family("minilisysm_sched_wait_us", {{30.0, {{"pid", "2"}, {"tid", "2"}}}});

    const std::string refreshed = registry.render_prometheus();
    CHECK(refreshed.find("minilisysm_sched_wait_us{pid=\"1\",tid=\"1\"}") == std::string::npos);
    CHECK(refreshed.find("minilisysm_sched_wait_us{pid=\"2\",tid=\"2\"} 30") != std::string::npos);
    return EXIT_SUCCESS;
}
