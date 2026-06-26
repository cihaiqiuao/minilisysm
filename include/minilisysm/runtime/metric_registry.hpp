#pragma once

#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace lisysm {

enum class MetricKind {
    Counter,
    Gauge,
};

struct MetricLabel {
    MetricLabel() = default;
    MetricLabel(std::string key_value, std::string label_value)
        : key(std::move(key_value)),
          value(std::move(label_value))
    {
    }
    std::string key;
    std::string value;
};

class MetricRegistry {
public:
    void set_gauge(const std::string& name, double value, std::vector<MetricLabel> labels = {});
    void inc_counter(const std::string& name, double delta = 1.0, std::vector<MetricLabel> labels = {});
    void set_counter(const std::string& name, double value, std::vector<MetricLabel> labels = {});
    std::string render_prometheus() const;

private:
    struct MetricValue {
        MetricKind kind{MetricKind::Gauge};
        double value{0.0};
        std::vector<MetricLabel> labels;
    };

    static std::string key_for(const std::string& name, const std::vector<MetricLabel>& labels);
    void set_locked(
        const std::string& name,
        MetricKind kind,
        double value,
        std::vector<MetricLabel> labels,
        bool add);

    mutable std::mutex mutex_;
    std::unordered_map<std::string, MetricValue> values_;
};

} // namespace lisysm
