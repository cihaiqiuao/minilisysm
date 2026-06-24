# 2026-06-23 Buffer Reuse and Collector Failure Events

## Completed

- Added reusable JSONL serialization buffer support in `EventSerializer`.
- Updated `EventStore` to reuse one `std::string` buffer inside the persistence worker instead of creating a new JSON line string for each event.
- Added `CollectorFailure` event publishing in `Monitor` for invalid fast-path collector samples.
- Added rate limiting for collector failure events to avoid flooding the event queue.
- Added sched-delay scan failure counting for directory-level failures while ignoring normal per-thread `/proc` races.
- Added `test_event_serializer` to cover reusable serialization and collector failure JSON output.

## Validation

- `cmake --build build` passed.
- `ctest --test-dir build --output-on-failure` passed, 4/4.
- `cmake --build build-asan` passed.
- `ctest --test-dir build-asan --output-on-failure` passed, 4/4.
- A short smoke run wrote `monitor_started` JSONL and did not emit false `CollectorFailure` events.
