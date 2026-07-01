# Linux 稳定性监控架构

## 目标边界

当前实现是 Linux 稳定性监控链路的工程化初版，重点是低风险、可扩展、可验证：

- 配置加载与校验。
- 轻量 `/proc` 指标采集。
- 可选 eBPF 调度延迟采集。
- 块设备 I/O 堵塞检测。
- 状态机规则判断。
- 固定大小内部事件。
- SPSC 有界队列。
- 后台 JSONL 滚动持久化。
- 单元测试、benchmark 和一键验证脚本。

## 工程分层

- `apps/minilisysm-agent/`：守护进程入口，只负责配置加载、信号处理和启动 `Monitor`。
- `include/minilisysm/`：核心库公共接口，按 `core`、`collectors`、`rules`、`queue`、`storage`、`runtime` 分层。
- `src/`：核心库实现，与公共接口模块对应。
- `ebpf/`：独立 eBPF 模块，包含 BPF 程序、libbpf 用户态 collector 和生成的 skeleton 接入。
- `configs/`：目标平台可调整参数，默认值保守。
- `tests/unit/`：不依赖真实目标机的基础单元测试。
- `tools/bench/`：性能与压测工具，当前包含 SPSC push/pop benchmark。
- `scripts/`：Ubuntu/WSL 依赖安装、构建和验证入口。
- `cmake/`：CMake 选项和编译器告警设置。

## 运行链路

```text
configs/*.ini
    -> apps/minilisysm-agent
       -> Monitor
          -> fast_collector
             -> MeminfoCollector / CpuUsageCollector / SelfStatusCollector / queue snapshot
             -> fast RuleEngine
             -> fast SpscRingBuffer<InternalEvent>
          -> sched_collector
             -> SchedDelayCollectorInterface
             -> SchedDelayCollector(/proc) or EbpfSchedDelayCollector(optional)
             -> IoDelayCollector(/proc/diskstats)
             -> sched RuleEngine
             -> sched SpscRingBuffer<InternalEvent>
          -> EventDispatcherGroup
             -> fast EventDispatcher
             -> sched EventDispatcher
             -> JsonlEventSink
             -> events_*.jsonl
```

当前 collector：

- `MeminfoCollector`：读取 `/proc/meminfo`，产出系统内存样本。
- `CpuUsageCollector`：读取 `/proc/stat`，按 jiffies 差分计算整体或分核心 CPU 使用率。
- `SelfStatusCollector`：读取 `/proc/self/status`，产出监控模块自身 RSS。
- `SchedDelayCollector`：读取 `/proc/<pid>/task/<tid>/sched`，产出调度等待和被动上下文切换增量。
- `EbpfSchedDelayCollector`：可选 eBPF 调度延迟采集器，启用 `MINILISYSM_ENABLE_EBPF` 且配置 `source=ebpf` 后由工厂创建；加载失败时回退 `/proc` collector。
- `IoDelayCollector`：读取 `/proc/diskstats`，按块设备计算平均 await、I/O 利用率和 in-flight 数。

当前规则：

- `MemoryPressure`：系统可用内存压力。
- `SelfRssPressure`：监控模块自身 RSS 压力。
- `CpuUsage`：整体 CPU 占用风险。
- `QueuePressure`：事件队列拥塞和丢弃。
- `SchedDelay`：目标线程调度等待风险。
- `IoDelay`：块设备 I/O 堵塞风险。

## I/O 堵塞检测

第一版 I/O 堵塞检测使用 `/proc/diskstats`，保持默认部署低依赖。collector 对每个设备保存上一次采样基线，并在下一轮计算：

- `delta_io_count`：本轮完成的读写 I/O 数。
- `avg_await_ms`：本轮读写耗时总和除以完成 I/O 数。
- `util_percent`：本轮设备忙碌时间占采样间隔的比例。
- `in_flight`：当前正在进行的 I/O 数。

规则层通过 `[io_delay_rule]` 的 await 和 util 阈值判断 `io_delay_risk`。事件的 `target` 字段记录设备名，evidence 记录 I/O 数、util、in-flight 和最大观测 await。

## CPU 占用检测

默认 CPU 占用检测使用 `/proc/stat` 第一行 `cpu`，在两次采样之间计算总 jiffies 和 idle jiffies 差分。配置 `mode=per_core` 或 `mode=both` 后，也会读取 `cpu0`、`cpu1` 等分核心行。

- `usage_percent`：`(delta_total - delta_idle) / delta_total * 100`。
- `delta_total_jiffies`：本轮总 CPU 时间增量。
- `delta_idle_jiffies`：本轮空闲 CPU 时间增量。

规则层通过 `[cpu_usage_rule]` 的 warning、critical 和 recovery 阈值判断 `cpu_usage_risk`。不同 CPU target 使用独立状态机，`total` 和 `cpu0`、`cpu1` 等核心不会互相影响告警/恢复状态。

## 快速路径约束

- 不进行磁盘写入。
- 不进行网络访问。
- 不构造 JSON。
- 不等待队列消费者。
- 队列满时记录丢弃计数并返回。

## 后台路径

`EventDispatcherGroup` 是采集队列后的分发层：

1. 为每条采集队列创建一个 `EventDispatcher`，例如 fast dispatcher 和 sched dispatcher。
2. 为每个 dispatcher 到每个 sink 分配一条专属 SPSC 队列，保持单生产者单消费者约束。
3. dispatcher 只消费自己的 source queue，并把事件 push 到各 sink 专属输入队列。
4. 单条 sink 输入队列满时只记录 dispatcher 统计，不影响其他 sink 输入队列。
5. 停止时先停止并 drain 各 dispatcher，再停止各 sink。

`JsonlEventSink` 是当前唯一落地的 sink：

1. 持有来自各 dispatcher 的多条输入 SPSC 队列。
2. 后台线程轮询这些输入队列，使用 `EventSerializer` 转为 JSONL。
3. 复用一个 `std::string` 序列化 buffer。
4. 按大小滚动文件。
5. 对 Critical 事件执行受限频率的 flush/fsync。
6. 根据缓存容量上限清理旧文件。

## eBPF 接入边界

调度延迟 collector 通过 `SchedDelayCollectorInterface` 暴露统一接口：

- 默认实现：`SchedDelayCollector`，使用 `/proc/<pid>/task/<tid>/sched`。
- 可选实现：`EbpfSchedDelayCollector`，作为 eBPF 数据源接入点。

`CollectorFactory` 根据 `[sched_delay_rule] source` 和编译开关选择实现。eBPF 数据源应转换为现有 `SchedDelaySample`，继续复用 `RuleEngine::evaluate_sched_delay()`，不要复制一套规则状态机。

## 性能验证

最低验证集：

```bash
./scripts/verify.sh
./scripts/verify.sh --asan
./scripts/verify.sh --ebpf
```

目标 Linux 平台还需要补充：

- `/proc` FD 复用与重复 open/close 对比。
- 监控开启前后的控制周期 P99/P999。
- 持久化线程与上传线程 CPU affinity 验证。
- fsync 频率和每小时写入字节数统计。
## 接口边界

`include/minilisysm/interfaces/` 是当前对外扩展边界，放置已经稳定下来的纯虚接口和跨实现数据结构。当前包括：

- `EventSink` / `SinkStats`：事件最终消费目标接口，`JsonlEventSink` 和 `NetworkEventSink` 都实现它。
- `SchedDelayCollectorInterface` / `SchedDelaySample` / `SchedDelayCollectorRuntimeStats`：调度延迟采集统一接口，`/proc` collector 和 eBPF collector 都实现它。

`collectors/`、`storage/`、`runtime/` 仍然保存内置实现。本次只是把接口从实现目录中抽出来，不实现 `.so` 动态插件加载，也不承诺 C++ ABI 稳定。后续如果要支持用户自定义 collector 或 sink 插件，应在这个接口层基础上单独设计插件注册、版本检查、加载失败隔离和配置 schema。
