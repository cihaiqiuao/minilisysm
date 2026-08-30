# 2026-07-03 Monitor metrics 解耦

## 本次完成

- 新增 `MonitorMetrics`，把 `Monitor` 中的 metrics 记录、动态 gauge family 刷新和 `/metrics` 渲染适配逻辑集中到独立 runtime helper。
- `Monitor` 保留采集调度、规则评估、事件发布和队列快照职责；metrics 相关调用改为传递样本、队列快照、sink stats、collector failure counters 和 eBPF runtime stats。
- 保持已有 metrics 名称、label、Prometheus 输出格式、事件格式和配置 schema 不变。
- 保留上一轮 `MetricRegistry::set_gauge_family()` 清理动态 `pid/tid` label 的行为。

## 设计边界

- 本次不拆 collector 调度循环、不改 `RuleEngine`、不改 dispatcher/sink 接口。
- `MonitorMetrics` 仍是 runtime 内部 helper，不引入插件化或新的 public extension point。
- `Monitor` 仍负责收集 `QueueSnapshot` 和 `EventDispatcherGroup::sink_stats()`，避免为了本次解耦扩大 dispatcher 接口。

## 验证结果

已通过：

```bash
cmake -S . -B /tmp/minilisysm-build-decouple -G Ninja \
  -DMINILISYSM_BUILD_TESTS=ON \
  -DMINILISYSM_BUILD_TOOLS=ON \
  -DMINILISYSM_ENABLE_EBPF=OFF
cmake --build /tmp/minilisysm-build-decouple
ctest --test-dir /tmp/minilisysm-build-decouple --output-on-failure
```

结果：

- no-vcpkg/system spdlog 构建通过。
- 全量 `15/15` CTest 通过。

## 后续建议

- 如果继续减轻 `Monitor`，下一步优先考虑把 collector failure registry / throttle 逻辑抽成内部 helper；不要一次性拆调度循环和事件发布路径。
