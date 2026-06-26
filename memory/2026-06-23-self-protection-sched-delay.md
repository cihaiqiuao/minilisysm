# 2026-06-23 自身保护与调度延迟规则

工作目录：`E:\minilisysm`

## 新增实现

- 增加 `SelfStatusCollector`，读取 `/proc/self/status` 中的 `VmRSS`。
- 增加 `SchedDelayCollector`，读取 `/proc/<pid>/task/<tid>/sched`，计算 `se.statistics.wait_sum` 和 `nr_involuntary_switches` 的采样增量。
- 新增配置段 `[self_protection]`，包含队列压力阈值、自身 RSS 软硬上限和恢复窗口。
- 新增配置段 `[sched_delay_rule]`，包含进程/线程白名单、wait_sum 阈值、被动上下文切换阈值、连续窗口和扫描目标上限。
- 扩展 `RuleId`：`SelfRssPressure = 2001`、`QueuePressure = 2002`、`SchedDelay = 3001`。
- 扩展 `EventType`：`MonitorMemoryPressure` 和 `SchedDelayRisk`。
- `Monitor::collect_loop` 现在会同时检查系统内存、自身 RSS、队列压力和调度延迟样本。

## 设计说明

- 自身 RSS 和队列压力属于监控模块自保护链路，用于避免监控模块自己消耗过多内存或队列积压。
- 调度延迟规则是 `/proc` 轻量版本，适合先做趋势检测；后续 eBPF 版本可用于毫秒级毛刺。
- `SchedDelayCollector` 在没有配置进程白名单时默认只扫描当前进程，避免初版扫描全系统造成额外开销。
- 线程白名单为空时会扫描目标进程下所有线程；配置后只扫描匹配线程名。

## 验证

- Release 构建通过。
- Release 单测通过：`spsc_queue`、`rule_engine`。
- ASan/UBSan 构建通过。
- ASan/UBSan 单测通过。
- 临时降低自身 RSS 阈值后，运行态烟测成功输出 `monitor_memory_pressure` 的 critical 事件。
- 默认配置短跑可以正常生成事件文件，调度扫描路径未出现运行异常。

## 剩余风险

- 调度延迟规则目前依赖 `/proc/<pid>/task/<tid>/sched` 字段和单位假设，目标平台需要校准。
- 队列压力规则目前基于采样时的 depth 和累计 drop count，后续可增加 drop 增量与冷却限频。
- 还没有做长时间压力测试，也没有和真实控制周期一起跑干扰验证。

## 多线程和多路 SPSC 重构

后续根据实时性讨论，将原来的单采集线程重构为两个采集 worker：

- `fast_collector`：系统内存、自身 RSS、队列压力等轻量规则。
- `sched_collector`：调度延迟扫描规则。

每个 worker 有独立的 `RuleEngine` 和独立的 `SpscRingBuffer<InternalEvent>`：

- `fast_queue`
- `sched_queue`

`EventStore` 从单队列消费者改为轮询多个 SPSC 队列。这样保持每个队列仍然满足单生产者单消费者约束，避免多个采集线程同时 push 到同一个 SPSC。

新增线程策略配置：

- `sched_collector_cpu`
- `sched_collector_nice`

当前默认仍为不绑核，目标平台确认 CPU 拓扑后再配置。

## 目录结构调整

项目从扁平的 `include/minilisysm/*.hpp` 和 `src/*.cpp` 调整为按模块分层：

- `core`：配置、事件模型、时间。
- `collectors`：`/proc` 和后续 Linux 指标采集器。
- `rules`：规则状态机。
- `queue`：SPSC 有界队列。
- `storage`：事件序列化与本地持久化。
- `runtime`：监控主循环和线程策略。

这样新增 collector、rule、storage 后台模块时可以放进明确边界，避免文件继续堆在同一层。
