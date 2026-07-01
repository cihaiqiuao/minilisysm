#include "minilisysm/collectors/collector_factory.hpp"

#include "minilisysm/collectors/cpu_usage_collector.hpp"
#include "minilisysm/collectors/io_delay_collector.hpp"
#include "minilisysm/collectors/meminfo_collector.hpp"
#include "minilisysm/collectors/sched_delay_collector.hpp"
#include "minilisysm/collectors/self_status_collector.hpp"

#if MINILISYSM_ENABLE_EBPF
#include "minilisysm/ebpf/ebpf_sched_delay_collector.hpp"
#endif

namespace lisysm {

std::unique_ptr<MeminfoCollector> CollectorFactory::create_meminfo_collector() {
    return std::make_unique<MeminfoCollector>();
}

std::unique_ptr<CpuUsageCollector> CollectorFactory::create_cpu_usage_collector(const MonitorConfig& config) {
    return std::make_unique<CpuUsageCollector>(config);
}

std::unique_ptr<SelfStatusCollector> CollectorFactory::create_self_status_collector() {
    return std::make_unique<SelfStatusCollector>();
}

std::unique_ptr<SchedDelayCollectorInterface>
CollectorFactory::create_sched_delay_collector(const MonitorConfig& config) {
#if MINILISYSM_ENABLE_EBPF
    if (config.sched_delay_source == "ebpf") {
        return std::make_unique<EbpfSchedDelayCollector>(config);
    }
#else
    (void)config;
#endif
    return std::make_unique<SchedDelayCollector>(config);
}

std::unique_ptr<IoDelayCollector> CollectorFactory::create_io_delay_collector(const MonitorConfig& config) {
    return std::make_unique<IoDelayCollector>(config);
}

} // namespace lisysm
