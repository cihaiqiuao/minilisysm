#pragma once

#include "minilisysm/core/config.hpp"
#include "minilisysm/interfaces/sched_delay_collector.hpp"

#include <memory>

namespace lisysm {

class MeminfoCollector;
class CpuUsageCollector;
class HardwareHealthCollector;
class IoDelayCollector;
class SchedDelayCollector;
class SelfStatusCollector;

class CollectorFactory {
  public:
    static std::unique_ptr<MeminfoCollector> create_meminfo_collector();
    static std::unique_ptr<CpuUsageCollector> create_cpu_usage_collector(const MonitorConfig& config);
    static std::unique_ptr<SelfStatusCollector> create_self_status_collector();
    static std::unique_ptr<HardwareHealthCollector> create_hardware_health_collector();
    static std::unique_ptr<SchedDelayCollectorInterface> create_sched_delay_collector(const MonitorConfig& config);
    static std::unique_ptr<IoDelayCollector> create_io_delay_collector(const MonitorConfig& config);
};

} // namespace lisysm
