# 2026-06-24 I/O 堵塞检测

## 已完成

- 新增 `IoDelayCollector`，默认通过 `/proc/diskstats` 采集块设备 I/O 状态。
- collector 对每个设备保存上一轮基线，下一轮计算读写 I/O 完成数、平均 await、设备利用率和 in-flight 数。
- 新增 `[io_delay_rule]` 配置段，支持设备白名单、await 阈值、util 阈值、连续触发窗口、恢复窗口和最大目标数。
- 新增 `io_delay_risk` 事件类型，事件 `target` 字段记录触发设备名。
- I/O 堵塞检测接入现有 `sched_collector` 线程，继续复用 `RuleEngine`、SPSC 队列和 JSONL 持久化链路。

## 设计说明

- 第一版使用 `/proc/diskstats`，保持低依赖和默认可部署。
- await 用读写耗时差分除以完成 I/O 数，适合观察块设备层面的排队/服务延迟。
- util 用设备忙碌时间除以采样间隔，配合 in-flight 判断设备是否持续繁忙。
- `device_whitelist` 为空时跳过 `loop`、`ram`、`fd` 等虚拟设备。

## 后续可扩展

- 增加 eBPF block tracepoint 数据源，采集更细粒度的请求生命周期。
- 为单个业务进程补充 `/proc/<pid>/io` 维度，区分系统级设备堵塞和进程级 I/O 增长。
- 增加设备名、分区和物理盘之间的映射策略，避免分区与整盘重复告警。
