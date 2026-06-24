#include "lisysm/rules/rule_factory.hpp"

#include "lisysm/rules/rule_engine.hpp"

namespace lisysm {

std::unique_ptr<RuleEngine> RuleFactory::create_fast_rule_engine(const MonitorConfig& config)
{
    return std::make_unique<RuleEngine>(config);
}

std::unique_ptr<RuleEngine> RuleFactory::create_sched_rule_engine(const MonitorConfig& config)
{
    return std::make_unique<RuleEngine>(config);
}

} // namespace lisysm
