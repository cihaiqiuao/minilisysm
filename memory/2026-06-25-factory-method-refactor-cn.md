# 2026-06-25 工厂方法重构中文记录

## 背景

项目早期 `Monitor` 直接创建 collector、rule engine 和 storage 对象。随着采集器数量增加，直接在 `Monitor` 中散落构造逻辑会让运行时编排和对象创建边界混在一起，不利于后续扩展 eBPF collector、I/O collector、多 sink 或测试替换。

因此引入工厂方法，将“创建什么对象”的逻辑从 `Monitor` 中抽离。

## 已完成

新增或调整的工厂类：

- `CollectorFactory`
  - 创建 `MeminfoCollector`
  - 创建 `SelfStatusCollector`
  - 根据配置创建 `/proc` 或 eBPF `SchedDelayCollector`
  - 创建 `IoDelayCollector`
- `RuleFactory`
  - 创建 fast path `RuleEngine`
  - 创建 sched path `RuleEngine`
- `StorageFactory`
  - 根据配置创建 `JsonlEventSink`
  - 根据配置创建 `NetworkEventSink`

`Monitor` 现在通过工厂拿到运行时组件，并使用 `std::unique_ptr` 持有对象。

## 为什么使用工厂方法

工厂方法的核心价值是把对象创建逻辑集中管理，避免 `Monitor` 既负责调度线程、采集循环、事件发布，又负责判断每类对象应该如何构造。

具体收益：

- 降低 `Monitor` 构造函数复杂度。
- eBPF 和 `/proc` collector 可以由 `CollectorFactory` 根据配置和编译开关选择。
- 新增 collector 或 sink 时，不需要在主流程中到处插构造逻辑。
- 单元测试可以直接验证 factory 返回非空对象。
- 构造边界更清晰，后续引入 mock 或替换实现更容易。

## 为什么暂时没有做统一 ICollector

当前 collector 返回的数据类型不同：

- `MeminfoCollector` 返回 `MeminfoSample`
- `SelfStatusCollector` 返回 `SelfStatusSample`
- `SchedDelayCollector` 返回 `std::vector<SchedDelaySample>`
- `IoDelayCollector` 返回 `std::vector<IoDelaySample>`

如果强行抽象成统一 `ICollector`，需要引入 variant、基类 sample 或事件化 collector，反而会增加复杂度。因此当前只抽象创建边界，不抽象所有 collector 的运行接口。

## 与当前架构的关系

工厂方法重构后，运行时结构保持不变：

- `fast_collector` 线程负责轻量采集和自保护规则。
- `sched_collector` 线程负责调度延迟和 I/O 堵塞采集。
- 两条 source SPSC 队列进入 `EventDispatcherGroup`。
- `StorageFactory` 注册 sink，当前包括 JSONL 和可选 NetworkSink。

## 验证

已有 `test_factories` 覆盖：

- collector factory 返回非空 collector。
- rule factory 返回非空 rule engine。
- storage factory 返回可用 sink 列表。

完整验证继续使用：

```bash
bash scripts/verify.sh
bash scripts/verify.sh --asan
bash scripts/verify.sh --ebpf
```

## 当前边界

- 工厂方法只管理创建逻辑，不负责生命周期运行。
- 队列拓扑仍由 `Monitor` 持有，因为它属于运行时线程模型的一部分。
- collector 接口没有完全统一，避免为了抽象而引入额外复杂度。
