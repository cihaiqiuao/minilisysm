#pragma once

#include "minilisysm/core/config.hpp"
#include "minilisysm/interfaces/event_sink.hpp"

#include <memory>
#include <vector>

namespace lisysm {

class StorageFactory {
public:
    static std::vector<std::unique_ptr<EventSink>> create_event_sinks(const MonitorConfig& config);
};

} // namespace lisysm
