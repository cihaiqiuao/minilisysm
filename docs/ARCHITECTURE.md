# Linux Stability Monitor Architecture

## 目标边界

当前实现是方案的工程化初版，优先完成低风险、可扩展、可验证的基础链路：

- 配置加载与校验。
- 轻量 `/proc` 指标采集。
- 状态机规则判断。
- 固定大小内部事件。
- SPSC 有界队列。
- 后台 JSONL 滚动持久化。
- 单元测试与队列 benchmark。

暂不在快速路径中实现网络上传、压缩、复杂 JSON 构造和 eBPF 用户态消费，这些能力应作为后台模块或独立 collector 逐步接入。

## 目录职责

- `include/lisysm/core/`：基础配置、事件模型和时间接口。
- `include/lisysm/collectors/`：Linux 指标采集器。
- `include/lisysm/rules/`：规则状态机、规则 ID 和规则事件构造。
- `include/lisysm/queue/`：快速路径与后台路径之间的有界事件通道。
- `include/lisysm/storage/`：事件序列化、文件滚动和本地缓存。
- `include/lisysm/runtime/`：监控主循环、线程策略和模块编排。
- `src/<module>/`：与公开头文件模块对应的实现目录。
- `config/`：目标平台可调整参数，默认值保守。
- `tests/`：不依赖 Linux `/proc` 的基础单元测试。
- `tools/`：性能与压测工具，当前包含 SPSC push/pop benchmark。
- `docs/`：工程约束、扩展说明和设计记录。

## 快速路径

快速路径由 `Monitor::collect_loop` 驱动：

1. 调用 collector 读取轻量指标。
2. 调用 `RuleEngine` 进行状态机判断。
3. 构造 `InternalEvent`。
4. 写入 `SpscRingBuffer`。

当前有两个生产者 worker：

- `fast_collector`：系统内存、自身 RSS、队列压力等轻量路径。
- `sched_collector`：调度延迟扫描路径。

两个 worker 各自拥有独立 SPSC 队列，后台 `EventStore` 线程轮询消费所有队列。这保留了 SPSC 的单生产者前提，也避免多个采集线程争用同一个 push 端。

当前 collector：

- `MeminfoCollector`：读取 `/proc/meminfo`，产出系统内存样本。
- `SelfStatusCollector`：读取 `/proc/self/status`，产出监控模块自身 RSS。
- `SchedDelayCollector`：读取 `/proc/<pid>/task/<tid>/sched`，产出调度等待和被动上下文切换增量。

当前规则：

- `MemoryPressure`：系统可用内存压力。
- `SelfRssPressure`：监控模块自身 RSS 压力。
- `QueuePressure`：事件队列拥塞和丢弃。
- `SchedDelay`：目标线程调度等待风险。

约束：

- 不进行磁盘写入。
- 不进行网络访问。
- 不构造 JSON。
- 不等待队列消费者。
- 队列满时记录丢弃计数并返回。

## 后台路径

`EventStore` 是当前后台消费者：

1. 从 SPSC 队列读取事件。
2. 使用 `EventSerializer` 转为 JSONL。
3. 按大小滚动文件。
4. 对 Critical 事件执行受限频率的 flush/fsync。
5. 根据缓存容量上限清理旧文件。

## 线程与绑核

线程策略来自 `[thread_policy]`：

- `fast_collector_cpu` / `fast_collector_nice`
- `sched_collector_cpu` / `sched_collector_nice`
- `persist_thread_cpu` / `background_nice`

默认 CPU 为 `-1`，表示不绑核。目标平台明确实时核心和后台核心后，可以把快速采集、调度扫描和持久化线程放到不同 CPU，避免与主控制线程竞争。

后续云端上传应从持久化文件读取批次，不应直接从采集线程发起请求。

## 新增采集器

新增 collector 时建议遵守：

- 初始化阶段打开稳定文件或检测能力。
- 采集阶段复用缓冲区。
- 使用 `std::string_view`、指针游标或 `std::from_chars`。
- 失败时返回 invalid sample，不抛异常终止主流程。
- 高开销采集器放到低频线程或后台线程。
- 如果新增独立采集线程，应为它配置独立 SPSC 队列，或改用经过 benchmark 的有界 MPSC。

## 新增规则

新增规则时建议遵守：

- 每条规则维护独立 `RuleContext`。
- 使用 Warning/Critical/Recovery 状态机。
- 触发阈值和恢复阈值分离。
- 事件必须携带阈值、窗口、连续命中次数和 evidence。
- 冷却、聚合和限频应在规则层或后台层实现，不能阻塞 collector。

## 性能验证

最低验证集：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
./build/bench_spsc
```

目标 Linux 平台还需要补充：

- `/proc` FD 复用与重复 open/close 对比。
- 监控开启前后的控制周期 P99。
- 持久化线程与上传线程 CPU affinity 验证。
- fsync 频率和每小时写入字节数统计。
