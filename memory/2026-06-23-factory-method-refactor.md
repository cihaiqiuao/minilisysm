# 2026-06-23 Factory Method Refactor

## Completed

- Added typed factory classes for construction boundaries:
  - `CollectorFactory`
  - `RuleFactory`
  - `StorageFactory`
- Updated `Monitor` to own collectors, rule engines, and event store through `std::unique_ptr`.
- Kept runtime behavior unchanged:
  - fast collector thread
  - sched collector thread
  - two SPSC producer queues
  - background `EventStore` consumer
- Added `test_factories` to verify all factories create valid components from default config.

## Design Notes

- No singleton was introduced because monitor components carry config, state, thread lifetime, or test-specific ownership.
- No generic `ICollector` was introduced yet because current collectors return different sample types.
- Queue creation remains inside `Monitor` because queue topology is still part of the runtime threading model.

## Validation

- `cmake --build build` passed.
- `ctest --test-dir build --output-on-failure` passed.
- `cmake --build build-asan` passed.
- `ctest --test-dir build-asan --output-on-failure` passed.
- A short smoke run wrote `monitor_started` to a temporary JSONL file under `/tmp/minilisysm_factory_smoke`.
