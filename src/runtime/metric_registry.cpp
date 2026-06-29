#include "minilisysm/runtime/metric_registry.hpp"

#include <algorithm>
#include <iomanip>
#include <sstream>

namespace lisysm {
namespace {

std::string escape_label(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size());
    for (const char c : value) {
        switch (c) {
        case '\\':
            escaped += "\\\\";
            break;
        case '"':
            escaped += "\\\"";
            break;
        case '\n':
            escaped += "\\n";
            break;
        default:
            escaped.push_back(c);
            break;
        }
    }
    return escaped;
}

} // namespace

void MetricRegistry::set_gauge(const std::string& name, double value, std::vector<MetricLabel> labels) {
    set_locked(name, MetricKind::Gauge, value, std::move(labels), false);
}

void MetricRegistry::inc_counter(const std::string& name, double delta, std::vector<MetricLabel> labels) {
    set_locked(name, MetricKind::Counter, delta, std::move(labels), true);
}

void MetricRegistry::set_counter(const std::string& name, double value, std::vector<MetricLabel> labels) {
    set_locked(name, MetricKind::Counter, value, std::move(labels), false);
}

std::string MetricRegistry::render_prometheus() const {
    std::vector<std::pair<std::string, MetricValue>> snapshot;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        snapshot.reserve(values_.size());
        for (const auto& item : values_) {
            snapshot.push_back(item);
        }
    }
    std::sort(snapshot.begin(), snapshot.end(), [](const auto& lhs, const auto& rhs) { return lhs.first < rhs.first; });

    std::ostringstream output;
    output << std::setprecision(12);
    std::string last_name;
    for (const auto& item : snapshot) {
        const size_t split = item.first.find('|');
        const std::string name = split == std::string::npos ? item.first : item.first.substr(0, split);
        const MetricValue& metric = item.second;
        if (name != last_name) {
            output << "# HELP " << name << " minilisysm metric.\n";
            output << "# TYPE " << name << ' ' << (metric.kind == MetricKind::Counter ? "counter" : "gauge") << "\n";
            last_name = name;
        }
        output << name;
        if (!metric.labels.empty()) {
            output << '{';
            for (size_t i = 0; i < metric.labels.size(); ++i) {
                if (i != 0) {
                    output << ',';
                }
                output << metric.labels[i].key << "=\"" << escape_label(metric.labels[i].value) << '"';
            }
            output << '}';
        }
        output << ' ' << metric.value << "\n";
    }
    return output.str();
}

std::string MetricRegistry::key_for(const std::string& name, const std::vector<MetricLabel>& labels) {
    std::string key = name;
    for (const MetricLabel& label : labels) {
        key.push_back('|');
        key += label.key;
        key.push_back('=');
        key += label.value;
    }
    return key;
}

void MetricRegistry::set_locked(const std::string& name, MetricKind kind, double value, std::vector<MetricLabel> labels,
                                bool add) {
    std::lock_guard<std::mutex> lock(mutex_);
    MetricValue& metric = values_[key_for(name, labels)];
    metric.kind = kind;
    metric.labels = std::move(labels);
    if (add) {
        metric.value += value;
    } else {
        metric.value = value;
    }
}

} // namespace lisysm
