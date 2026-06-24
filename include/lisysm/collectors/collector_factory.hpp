#pragma once

#include "lisysm/core/config.hpp"

#include <memory>

namespace lisysm {

class MeminfoCollector;
class SchedDelayCollector;
class SelfStatusCollector;

class CollectorFactory {
public:
    static std::unique_ptr<MeminfoCollector> create_meminfo_collector();
    static std::unique_ptr<SelfStatusCollector> create_self_status_collector();
    static std::unique_ptr<SchedDelayCollector> create_sched_delay_collector(const MonitorConfig& config);
};

} // namespace lisysm
