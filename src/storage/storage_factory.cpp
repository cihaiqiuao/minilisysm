#include "minilisysm/storage/storage_factory.hpp"

#include "minilisysm/storage/jsonl_event_sink.hpp"
#include "minilisysm/storage/network_event_sink.hpp"

namespace lisysm {

std::vector<std::unique_ptr<EventSink>> StorageFactory::create_event_sinks(const MonitorConfig& config)
{
    std::vector<std::unique_ptr<EventSink>> sinks;
    if (config.persistence_enable) {
        sinks.push_back(std::make_unique<JsonlEventSink>(config));
    }
    if (config.network_sink_enable) {
        sinks.push_back(std::make_unique<NetworkEventSink>(config));
    }
    return sinks;
}

} // namespace lisysm
