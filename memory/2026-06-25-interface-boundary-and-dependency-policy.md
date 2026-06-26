# 接口边界抽取与第三方依赖管理预留

## 背景

项目已经有多个 collector、dispatcher 和 sink，后续可能继续增加采集器、网络上报和自定义扩展。如果纯虚接口继续放在具体业务目录里，调用方会更容易依赖实现细节，`CollectorFactory`、`StorageFactory` 和 runtime 层也会越来越难维护。

本次只做低风险工程化整理：抽出稳定接口边界，不实现 `.so` 动态插件系统，也不新增实际第三方库。

## 已完成

- 新增 `include/minilisysm/interfaces/event_sink.hpp`，承载 `EventSink` 和 `SinkStats`。
- 新增 `include/minilisysm/interfaces/sched_delay_collector.hpp`，承载 `SchedDelaySample`、`SchedDelayCollectorRuntimeStats` 和 `SchedDelayCollectorInterface`。
- `JsonlEventSink`、`NetworkEventSink`、`StorageFactory`、`EventDispatcherGroup` 改为依赖 `interfaces/event_sink.hpp`。
- `RuleEngine` 和 eBPF 调度延迟 collector 改为依赖 `interfaces/sched_delay_collector.hpp`。
- 保留旧头 `storage/event_sink.hpp` 作为兼容转发，保留 `collectors/sched_delay_collector.hpp` 作为具体 `/proc` collector 实现头。
- 新增接口编译单测，确保外部代码可以只 include `interfaces/` 下的头完成派生和调用。
- 新增 `third_party/README.md` 和 `docs/DEPENDENCIES.md`，明确当前不 vendoring 第三方库，未来引入外部库时统一通过 `third_party/` 或 vcpkg/Conan 管理。

## 边界

- 本次没有实现动态插件加载、ABI 稳定层、`.so` 生命周期管理或插件配置 schema。
- `EventSink::add_input_queue(size_t capacity)` 仍返回 `SpscRingBuffer<InternalEvent>*`，暂时保留现有队列耦合，避免扩大重构范围。
- 第三方依赖只建立管理规则，没有提前添加空的 vcpkg/Conan manifest。

## 后续

如果后续要支持用户自定义采集器插件，建议单独设计：

- 纯 C ABI 或稳定 C++ ABI 边界。
- 插件元数据和版本兼容检查。
- `.so` 加载、卸载和失败隔离。
- collector/sink 注册表，而不是继续扩大 `CollectorFactory`。
