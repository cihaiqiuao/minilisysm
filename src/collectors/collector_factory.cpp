#include "lisysm/collectors/collector_factory.hpp"

#include "lisysm/collectors/meminfo_collector.hpp"
#include "lisysm/collectors/sched_delay_collector.hpp"
#include "lisysm/collectors/self_status_collector.hpp"

namespace lisysm {

std::unique_ptr<MeminfoCollector> CollectorFactory::create_meminfo_collector()
{
    return std::make_unique<MeminfoCollector>();
}

std::unique_ptr<SelfStatusCollector> CollectorFactory::create_self_status_collector()
{
    return std::make_unique<SelfStatusCollector>();
}

std::unique_ptr<SchedDelayCollector> CollectorFactory::create_sched_delay_collector(const MonitorConfig& config)
{
    return std::make_unique<SchedDelayCollector>(config);
}

} // namespace lisysm
