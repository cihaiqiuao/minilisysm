#pragma once

#include "minilisysm/collectors/linux_proc_reader.hpp"

#include <cstdint>

namespace lisysm {

struct MeminfoSample {
    uint64_t mem_total_kb{0};
    uint64_t mem_available_kb{0};
    uint64_t swap_free_kb{0};
    uint64_t sreclaimable_kb{0};
    uint64_t sunreclaim_kb{0};
    bool valid{false};
};

class MeminfoCollector {
public:
    MeminfoCollector();
    MeminfoSample collect();

private:
    LinuxProcReader reader_;
};

} // namespace lisysm
