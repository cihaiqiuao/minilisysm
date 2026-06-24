#include "lisysm/storage/storage_factory.hpp"

#include "lisysm/storage/event_store.hpp"

namespace lisysm {

std::unique_ptr<EventStore> StorageFactory::create_event_store(
    const MonitorConfig& config,
    std::vector<SpscRingBuffer<InternalEvent>*>& queues)
{
    return std::make_unique<EventStore>(config, queues);
}

} // namespace lisysm
