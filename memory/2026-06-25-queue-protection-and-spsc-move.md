# 2026-06-25 队列保护机制与 SPSC 移动语义

## 背景

项目从单一落盘消费链路扩展为多采集线程、多 source queue、多 dispatcher、多 sink 队列后，队列满、慢 sink、事件丢弃和 Critical 事件保留都变成需要明确处理的问题。同时，`SpscRingBuffer` 后续可能不只传递简单 POD 风格事件，还可能传递包含 `std::string`、动态 evidence、智能指针或 move-only payload 的对象，因此补齐移动语义是必要的基础能力。

## 队列保护方案

### source queue 保护

`Monitor` 仍然拥有两条 source queue：

- `fast_queue_`：系统内存、自身 RSS、队列压力等快速路径事件。
- `sched_queue_`：调度延迟、I/O 堵塞等低频或较重采集事件。

每条队列都是有界 `SpscRingBuffer<InternalEvent>`，通过配置控制容量和保留槽位：

```ini
[event_queue]
capacity=4096
critical_reserved_slots=32
drop_info_when_full=true
drop_warning_when_full=true
```

保护策略：

- 队列接近满时，Info 事件优先被拒绝。
- Warning 事件可配置为在满载压力下被拒绝。
- Critical 事件可以使用保留槽位，避免普通事件把队列完全占满后导致关键报警无法进入队列。
- 如果保留槽位也耗尽，Critical 仍可能被拒绝，但会计入 `dropped_critical_count`。

### sink queue 保护

`EventDispatcherGroup` 会给每个 source queue 到每个 sink 分配一条独立 SPSC 队列。这样慢 sink 不会直接阻塞采集线程，也不会阻塞 dispatcher 向其他 sink 投递。

当前 sink 包括：

- `JsonlEventSink`
- `NetworkEventSink`

每个 sink 的内部队列同样使用有界 SPSC，并复用相同的容量、丢弃和 Critical 保留策略。

### 队列压力统计

`SpscRingBuffer` 维护以下统计：

- `push_fail_count`
- `dropped_info_count`
- `dropped_warning_count`
- `dropped_critical_count`
- `reserve_reject_count`
- `high_watermark`

`EventDispatcherGroup` 聚合 sink 队列状态，`Monitor::queue_snapshot()` 会同时采集 source queue 和 sink queue 的压力。

`RuleEngine::evaluate_queue()` 使用 source/sink 队列中的最大压力作为主判断，并结合以下外部触发条件：

- 新增 drop。
- 新增 Critical drop。
- dispatcher 向 sink 队列投递失败。

队列压力事件 evidence 包含：

- `source_queue_percent`
- `sink_queue_percent`
- `total_dropped_count`
- `dispatcher_failures`
- `critical_dropped_count`
- `high_watermark_percent`

## SPSC 移动语义

### 修改内容

`SpscRingBuffer` 现在同时支持：

```cpp
bool push(const T& item, EventLevel level);
bool push(T&& item, EventLevel level);
```

内部通过完美转发写入：

```cpp
buffer_[head] = std::forward<U>(item);
```

`pop()` 使用移动语义：

```cpp
item = std::move(buffer_[tail]);
```

### 为什么要这么做

之前如果只支持拷贝：

- `InternalEvent` 当前比较简单，拷贝成本还可以接受。
- 但未来如果事件中加入 `std::string`、`std::vector`、动态 evidence 或 `std::unique_ptr`，拷贝会带来性能损耗，甚至无法编译。

移动语义的价值：

- 对 `std::string`、`std::vector` 等对象，移动通常只转移内部指针和容量信息，避免重新分配和复制。
- 对 `std::unique_ptr` 这类独占所有权对象，必须移动，不能拷贝。
- 队列可以成为更通用的高性能传输组件，而不是只能传简单结构体。

### 验证

`test_spsc.cpp` 增加 move-only 类型测试：

- 使用 `std::unique_ptr<int>` 作为队列元素。
- `push(std::move(ptr))` 后所有权进入队列。
- `pop(out)` 后所有权从队列转移到输出变量。

这证明当前队列支持 move-only payload。

## 当前边界

- 队列仍是 SPSC，不支持多个生产者或多个消费者直接读写同一条队列。
- 队列满时不会阻塞生产者，采用丢弃和统计方式保护监控主流程。
- Critical 事件是“尽量保留”，不是绝对不丢；极端满载下仍可能被拒绝，但会被统计和暴露。
