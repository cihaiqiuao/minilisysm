#pragma once

#include "minilisysm/core/config.hpp"
#include "minilisysm/collectors/sched_delay_collector.hpp"
#include "minilisysm/interfaces/sched_delay_collector.hpp"

#include <cstdint>
#include <memory>
#include <vector>

namespace lisysm {

class EbpfSchedDelayCollector : public SchedDelayCollectorInterface {
  public:
    explicit EbpfSchedDelayCollector(const MonitorConfig& config);
    ~EbpfSchedDelayCollector() override;

    EbpfSchedDelayCollector(const EbpfSchedDelayCollector&) = delete;
    EbpfSchedDelayCollector& operator=(const EbpfSchedDelayCollector&) = delete;

    std::vector<SchedDelaySample> collect() override;
    uint64_t last_failure_count() const override {
        return last_failure_count_;
    }
    SchedDelayCollectorRuntimeStats runtime_stats() const override;

  private:
    struct Impl;

    bool accepts(int32_t pid, int32_t tid) const;

    const MonitorConfig& config_;
    std::unique_ptr<Impl> impl_;
    std::unique_ptr<SchedDelayCollector> fallback_;
    uint64_t last_failure_count_{0};
    bool initialized_{false};
};

} // namespace lisysm
