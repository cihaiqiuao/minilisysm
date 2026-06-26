# 2026-06-23 Buffer 复用与采集器失败事件

## 已完成

- 在 `EventSerializer` 中增加可复用 JSONL 序列化 buffer 支持。
- 更新 `EventStore`，在持久化 worker 内复用一个 `std::string` buffer，避免每条事件都创建新的 JSON 行字符串。
- 在 `Monitor` 中为无效的快速路径 collector sample 增加 `CollectorFailure` 事件发布。
- 为 collector failure 事件增加限频，避免异常时刷爆事件队列。
- 增加调度延迟扫描失败计数：统计目录级失败，同时忽略正常的单线程 `/proc` 竞争。
- 增加 `test_event_serializer`，覆盖可复用序列化和 collector failure JSON 输出。

## 验证

- `cmake --build build` 通过。
- `ctest --test-dir build --output-on-failure` 通过，4/4。
- `cmake --build build-asan` 通过。
- `ctest --test-dir build-asan --output-on-failure` 通过，4/4。
- 短时间 smoke run 成功写出 `monitor_started` JSONL，且没有误报 `CollectorFailure` 事件。
