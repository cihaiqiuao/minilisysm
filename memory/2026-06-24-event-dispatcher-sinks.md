# 2026-06-24 事件消费链路重构

## 已完成

- 将原来 `EventStore` 直接轮询采集队列并落盘的结构，重构为 `EventDispatcher + EventSink`。
- 新增 `EventSink` 接口，表示事件最终消费目标。
- 新增 `EventDispatcherGroup`，为 `fast_queue_` 和 `sched_queue_` 分别创建独立 `EventDispatcher`。
- 每个 dispatcher 到每个 sink 都分配一条专属 SPSC 队列，避免多个 dispatcher 同时写同一个队列。
- 将 JSONL 落盘逻辑迁移为 `JsonlEventSink`，保留 JSONL 序列化、文件滚动、缓存容量控制和 critical fsync 限流。
- `JsonlEventSink` 持有多条输入 SPSC 队列，后台线程轮询这些队列并落盘。
- `Monitor` 不再直接持有 `EventStore`，改为持有 `EventDispatcherGroup`。

## 设计说明

- collector 和 rule 层保持不变，仍由 `Monitor` 产生 `InternalEvent`。
- 第一版只注册 `JsonlEventSink`，为后续 `UploadSink`、`MetricsSink`、`AlertSink` 留出扩展点。
- 单条 sink 输入队列满时不会阻塞 dispatcher 对其他 sink 输入队列的投递。
- `persistence_enable=false` 时不注册 JSONL sink，dispatcher 仍可启动，方便后续只启用其他 sink。

## 已验证

- Release 构建通过，单测扩展到 6 个。
- 新增 `event_dispatcher` 单测覆盖单 source dispatcher、sink 专属输入队列和 dispatcher group。
- 新增 `jsonl_event_sink` 单测覆盖 JSONL 写出和统计。
