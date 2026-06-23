#pragma once

#include <cstdint>

namespace lisysm {

uint64_t realtime_ms();
uint64_t monotonic_ms();
uint64_t boottime_ms();

} // namespace lisysm
