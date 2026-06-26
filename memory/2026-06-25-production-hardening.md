# 2026-06-25 生产化增强与可观测性补齐

## 背景

本轮是在工程化目录、eBPF 调度延迟采集、事件 dispatcher/sink 架构、队列保护方案已经完成的基础上，继续补齐“生产级可观测性与远端上报”能力。目标不是改变现有 collector/rule/JSONL 主链路，而是在保持兼容的前提下增强 Metrics、Network Sink、WAL、eBPF 生命周期维护、eBPF 聚合和 RuleEngine 模板化。

## 第一轮完成内容

### Metrics pipeline 基础版

- 新增 `MetricRegistry`，将持续指标与告警事件拆开。
- `/metrics` 继续由 `MetricsServer` 暴露，输出 Prometheus text format。
- `Monitor` 在采集周期内写入核心指标：
  - 内存总量、可用内存。
  - 监控进程自身 RSS。
  - source/sink 队列深度、容量、丢弃数、高水位。
  - I/O await、util、delta I/O count。
  - 调度等待、非自愿切换次数。
  - collector failure、collector elapsed、overrun、事件发布计数。

### NetworkEventSink 基础版

- 新增 `NetworkEventSink`，实现 `EventSink` 接口。
- 通过 `StorageFactory` 根据 `[network_sink] enable=true` 注册。
- 复用现有 `EventDispatcherGroup`，每个 source queue 会给 network sink 分配独立 SPSC 输入队列。
- 第一版使用 HTTP 批量 POST，不引入 gRPC/Kafka/curl。
- 发送前写 WAL，HTTP 2xx 后清理，失败后按指数退避重试。

### eBPF 生命周期与聚合基础版

- `sched_delay.bpf.c` 增加 `sched_process_exec` 和 `sched_process_exit` tracepoint。
- exit 时清理 `pid_allowlist`、`tid_allowlist`、`runnable_since`，降低 PID/TID 复用导致的短时间误命中。
- 增加 eBPF aggregate map，支持按 pid/tid 聚合调度等待后再上报。
- 聚合默认关闭，避免改变默认 eBPF 行为。

### RuleEngine 模板化起步

- 新增 `ThresholdRuleDefinition`。
- memory、self RSS、sched delay 先接入通用阈值状态机。
- 事件构造函数仍保留，避免改变 JSONL 字段和 rule_id。

## 第二轮补齐内容

### NetworkEventSink 和 WAL 补强

- 将单文件 `events.wal` 改成 segment WAL，文件名形如 `events_000001.wal`。
- 新增配置项 `network_wal_segment_mb`。
- 启动时扫描 WAL segment，恢复未 ack 的事件。
- HTTP 2xx 后按 batch ack，并重写 WAL，保留未发送成功的数据。
- 断网或 HTTP 失败时保留 WAL，后续继续补传。
- 实现 `network_wal_max_mb` 容量保护：
  - WAL 超限时停止接收非 Critical 事件。
  - Critical 事件尽量保留。
  - 增加 `wal_overflow_dropped_events` 统计。
- connect 改为非阻塞 `connect + poll`，开始真正使用 `network_connect_timeout_ms`。

### eBPF 生命周期补齐

- `sched_process_exec` 不在内核态做 comm 字符串匹配，而是累加 `allowlist_exec_seen`。
- 用户态 `EbpfSchedDelayCollector` 每次 poll 读取 exec counter delta。
- 如果配置了 whitelist 且发现 exec delta，立即刷新 `/proc` allowlist，不再只能等固定刷新周期。
- allowlist miss 时累加 `allowlist_stale_hits`，用于观测 PID/TID 复用或 allowlist 过期风险。
- 用户态记录 allowlist 刷新统计：
  - scanned processes。
  - matched pids。
  - matched tids。
  - refresh elapsed ms。

### eBPF 聚合补齐

- 扩展 BPF ringbuf event：
  - `max_wait_ns`
  - `avg_wait_ns`
  - `aggregate_count`
  - `flags`
- 用户态 `SchedDelaySample` 增加：
  - `max_wait_us`
  - `avg_wait_us`
  - `aggregate_count`
- `/proc` fallback 也填充这些字段，保持规则输入结构一致。
- `RuleEngine::make_sched_delay_event()` evidence 增加：
  - `max_wait_us`
  - `avg_wait_us`
  - `aggregate_count`
- CMake 增加 `MINILISYSM_EBPF_AGGREGATE_MAX_ENTRIES`，用于编译期控制 eBPF aggregate map 容量。

### RuleEngine 模板化补齐

- queue 规则接入通用 `ThresholdRuleDefinition`：
  - 主阈值为 `max(source_queue_percent, sink_queue_percent)`。
  - drop、critical drop、dispatcher failure 作为 external trigger。
- I/O 规则接入通用 `ThresholdRuleDefinition`：
  - 主阈值为 `avg_await_ms`。
  - `util_percent + in_flight` 作为 external trigger。
- 保留原有事件构造函数，JSONL schema 兼容。

### Metrics 收口

- `MetricRegistry` 输出 `HELP` 和稳定 `TYPE`。
- `EventDispatcherGroup` 增加 `sink_stats()`，可按 sink 名称暴露统计。
- `/metrics` 新增 network sink、WAL、eBPF 生命周期、eBPF 聚合相关指标：
  - `minilisysm_network_sent_total`
  - `minilisysm_network_send_errors_total`
  - `minilisysm_network_retries_total`
  - `minilisysm_network_wal_pending_events`
  - `minilisysm_network_wal_bytes`
  - `minilisysm_network_wal_overflow_dropped_total`
  - `minilisysm_ebpf_allowlist_exec_seen_total`
  - `minilisysm_ebpf_allowlist_exit_cleaned_total`
  - `minilisysm_ebpf_allowlist_stale_hits_total`
  - `minilisysm_ebpf_allowlist_refresh_elapsed_ms`
  - `minilisysm_sched_max_wait_us`
  - `minilisysm_sched_avg_wait_us`
  - `minilisysm_sched_aggregate_count`

## 新增或调整的关键配置

```ini
[network_sink]
enable=false
endpoint=http://127.0.0.1:8080/events
batch_size=128
flush_interval_ms=1000
connect_timeout_ms=500
request_timeout_ms=2000
retry_base_ms=1000
retry_max_ms=60000
wal_path=./lisysm_wal
wal_max_mb=64
wal_segment_mb=4

[sched_delay_rule]
ebpf_lifecycle_enable=true
ebpf_allowlist_refresh_ms=10000
ebpf_aggregate_enable=false
ebpf_aggregate_window_ms=1000
ebpf_aggregate_max_entries=8192
```

## 验证结果

三套验证均通过：

```bash
bash scripts/verify.sh
bash scripts/verify.sh --asan
bash scripts/verify.sh --ebpf
```

验证结果：

- Release 构建通过。
- 9/9 单元测试通过。
- benchmark 通过。
- ASan/UBSan 构建与单测通过。
- eBPF skeleton 生成通过。
- eBPF root smoke 可启动并写出 JSONL。

## 新增测试覆盖

- `MetricRegistry` counter/gauge/label 渲染。
- `NetworkEventSink` HTTP 2xx ack 后清理 WAL。
- `NetworkEventSink` 断网时保留 WAL。
- eBPF 构建链路继续覆盖 skeleton 生成和 smoke。

## 当前边界

- WAL 已具备 segment、ack、恢复和容量保护，但还不是数据库级 WAL：
  - 未实现 CRC 校验。
  - 未实现压缩。
  - 未实现严格事务 fsync。
- NetworkSink 使用 HTTP 批量 POST，语义为至少一次，允许服务端按 event_id 去重。
- eBPF 不在内核态做 comm 字符串匹配，exec 只触发用户态快速刷新 allowlist。
- RuleEngine 已完成阈值模板化收口，但还没有引入 YAML/JSON DSL，也没有做热更新。
- eBPF 聚合默认关闭，作为高上下文切换压力下的可选优化。

## 面试或简历可讲亮点

- 将单一 JSONL 落盘链路演进为 `EventDispatcher + 多 EventSink`，支持本地持久化和远端上报并行扩展。
- 引入 Prometheus `/metrics`，把报警事件和连续时序指标拆开，增强可观测性。
- 为 NetworkSink 增加 WAL 和至少一次补传语义，提升断网恢复能力。
- eBPF 调度延迟采集从单事件上报扩展到 allowlist 生命周期维护和内核侧聚合，降低事件风暴风险。
- RuleEngine 从硬编码分支逐步抽象成阈值模板，减少重复状态机逻辑。
