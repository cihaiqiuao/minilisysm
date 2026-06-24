#include "lisysm/queue/spsc_ring_buffer.hpp"

#include <cstdlib>
#include <iostream>
#include <thread>

#define CHECK(condition)                                                                            \
    do {                                                                                            \
        if (!(condition)) {                                                                         \
            std::cerr << "check failed: " #condition << " at line " << __LINE__ << "\n";          \
            return EXIT_FAILURE;                                                                    \
        }                                                                                           \
    } while (false)

int main()
{
    lisysm::SpscRingBuffer<lisysm::InternalEvent> queue(8);
    lisysm::InternalEvent event;
    event.sequence = 42;
    CHECK(queue.push(event));
    lisysm::InternalEvent out;
    CHECK(queue.pop(out));
    CHECK(out.sequence == 42);

    for (int i = 0; i < 8; ++i) {
        event.sequence = static_cast<uint64_t>(i);
        CHECK(queue.push(event));
    }
    CHECK(!queue.push(event, lisysm::EventLevel::Warning));
    CHECK(queue.stats().dropped_warning_count == 1);

    for (int i = 0; i < 8; ++i) {
        CHECK(queue.pop(out));
        CHECK(out.sequence == static_cast<uint64_t>(i));
    }
    CHECK(!queue.pop(out));
    return 0;
}
