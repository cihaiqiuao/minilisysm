#include "minilisysm/queue/spsc_ring_buffer.hpp"

#include <cstdlib>
#include <iostream>
#include <memory>
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

    lisysm::SpscRingBuffer<lisysm::InternalEvent> protected_queue(4, 1, true, true);
    event.level = lisysm::EventLevel::Info;
    CHECK(protected_queue.push(event, event.level));
    CHECK(protected_queue.push(event, event.level));
    CHECK(protected_queue.push(event, event.level));
    CHECK(!protected_queue.push(event, lisysm::EventLevel::Info));
    CHECK(protected_queue.stats().reserve_reject_count == 1);
    CHECK(protected_queue.stats().dropped_info_count == 1);
    CHECK(protected_queue.push(event, lisysm::EventLevel::Critical));
    CHECK(!protected_queue.push(event, lisysm::EventLevel::Critical));
    CHECK(protected_queue.stats().dropped_critical_count == 1);

    lisysm::SpscRingBuffer<lisysm::InternalEvent> warning_protected_queue(4, 1, true, true);
    CHECK(warning_protected_queue.push(event, lisysm::EventLevel::Warning));
    CHECK(warning_protected_queue.push(event, lisysm::EventLevel::Warning));
    CHECK(warning_protected_queue.push(event, lisysm::EventLevel::Warning));
    CHECK(!warning_protected_queue.push(event, lisysm::EventLevel::Warning));
    CHECK(warning_protected_queue.stats().dropped_warning_count == 1);

    lisysm::SpscRingBuffer<std::unique_ptr<int>> move_queue(2);
    auto owned = std::make_unique<int>(7);
    CHECK(move_queue.push(std::move(owned)));
    CHECK(owned == nullptr);
    std::unique_ptr<int> moved_out;
    CHECK(move_queue.pop(moved_out));
    CHECK(moved_out != nullptr);
    CHECK(*moved_out == 7);
    return 0;
}
