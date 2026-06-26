# 2026-06-23 工厂方法重构

## 已完成

- 新增按职责划分的工厂类，用于收敛对象创建边界：
  - `CollectorFactory`
  - `RuleFactory`
  - `StorageFactory`
- `Monitor` 通过 `std::unique_ptr` 持有采集器、规则引擎和事件存储对象，生命周期更清晰。
- 运行行为保持不变：
  - fast 采集线程不变。
  - sched 采集线程不变。
  - 两条 SPSC 生产队列不变。
  - 后台 `EventStore` 消费线程不变。
- 新增 `test_factories`，验证默认配置下各个工厂都能创建有效组件。

## 设计说明

- 没有引入单例，因为监控组件都携带配置、状态、线程生命周期或测试所需的独立所有权。
- 暂时没有引入统一的 `ICollector`，因为当前采集器返回的 sample 类型不同，强行统一会扩大改动范围。
- 队列创建仍保留在 `Monitor` 中，因为队列拓扑属于 runtime 线程模型的一部分。

## 验证

- `cmake --build build` 通过。
- `ctest --test-dir build --output-on-failure` 通过。
- `cmake --build build-asan` 通过。
- `ctest --test-dir build-asan --output-on-failure` 通过。
- 短时间 smoke 运行能在 `/tmp/minilisysm_factory_smoke` 下写出包含 `monitor_started` 的 JSONL 文件。
