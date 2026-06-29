#pragma once

#include "minilisysm/core/config.hpp"

#include <memory>

namespace lisysm {

class RuleEngine;

class RuleFactory {
  public:
    static std::unique_ptr<RuleEngine> create_fast_rule_engine(const MonitorConfig& config);
    static std::unique_ptr<RuleEngine> create_sched_rule_engine(const MonitorConfig& config);
};

} // namespace lisysm
