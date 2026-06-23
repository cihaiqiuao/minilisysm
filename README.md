# minilisysm

`minilisysm` 是 Lisysm Linux 稳定性监控链路的初版工程实现。当前版本聚焦方案中的第一阶段和部分实时性优化：配置驱动、轻量 `/proc` 采集、规则状态机、固定大小内部事件、SPSC 有界无锁队列、后台 JSONL 滚动持久化，以及基础性能 benchmark。

## 架构

```text
config/*.ini
    -> Monitor
       -> MeminfoCollector / future collectors
       -> RuleEngine
       -> SpscRingBuffer<InternalEvent>
       -> EventStore background thread
```

快速路径原则：

- 不做网络请求。
- 不做 JSON 序列化。
- 事件对象定长，队列容量固定。
- 队列满时按事件等级降级丢弃，不阻塞采集线程。
- `/proc` 系统级文件优先 FD 复用，失败后降级重新打开。

后台路径负责：

- 结构化事件序列化。
- 文件滚动和缓存容量保护。
- Critical 事件受控同步刷盘。
- 后续可扩展异步上传、压缩和断网补传。

## 构建

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Windows 环境可完成编译和单元测试；Linux 目标机运行时会启用 `/proc` 采集、线程 nice 和 CPU affinity。

## 运行

```bash
./build/minilisysm config/lisysm_monitor.ini
```

默认缓存目录为 `./lisysm_events`，事件文件按大小滚动为 `events_000001.jsonl`。

## 后续扩展点

- 新增采集器：实现 `ICollector`，只返回数值快照，不直接落盘或上传。
- 新增规则：在 `RuleEngine` 中注册规则上下文，保留状态机、防抖和恢复事件。
- 上传闭环：在后台消费文件批次，按 `event_id = device_id + boot_id + sequence` 保证云端幂等。
- eBPF 增强：新增独立 collector，将内核侧聚合结果作为 Evidence 写入事件。
