#pragma once

#include "minilisysm/collectors/linux_proc_reader.hpp"

#include <cstdint>

namespace lisysm {

struct SelfStatusSample {
    uint64_t vm_rss_kb{0};
    bool valid{false};
};

class SelfStatusCollector {
public:
    SelfStatusCollector();
    SelfStatusSample collect();

private:
    LinuxProcReader reader_;
};

} // namespace lisysm
