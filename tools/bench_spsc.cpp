#include "lisysm/spsc_ring_buffer.hpp"
#include "lisysm/time.hpp"

#include <iostream>

int main()
{
    constexpr uint64_t kIterations = 1000000;
    lisysm::SpscRingBuffer<lisysm::InternalEvent> queue(4096);
    lisysm::InternalEvent event;
    lisysm::InternalEvent out;

    const uint64_t start = lisysm::monotonic_ms();
    for (uint64_t i = 0; i < kIterations; ++i) {
        event.sequence = i;
        while (!queue.push(event)) {
            queue.pop(out);
        }
        queue.pop(out);
    }
    const uint64_t elapsed = lisysm::monotonic_ms() - start;
    std::cout << "iterations=" << kIterations << " elapsed_ms=" << elapsed
              << " avg_ns_per_push_pop="
              << (elapsed == 0 ? 0 : (elapsed * 1000000ULL / kIterations)) << "\n";
    return 0;
}
