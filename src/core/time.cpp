#include "minilisysm/core/time.hpp"

#include <chrono>

#if defined(__linux__)
#include <ctime>
#endif

namespace lisysm {
namespace {

uint64_t chrono_ms(std::chrono::nanoseconds ns) {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(ns).count());
}

} // namespace

uint64_t realtime_ms() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
            .count());
}

uint64_t monotonic_ms() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

uint64_t boottime_ms() {
#if defined(__linux__)
    timespec ts{};
    if (clock_gettime(CLOCK_BOOTTIME, &ts) == 0) {
        return chrono_ms(std::chrono::seconds(ts.tv_sec) + std::chrono::nanoseconds(ts.tv_nsec));
    }
#endif
    return monotonic_ms();
}

} // namespace lisysm
