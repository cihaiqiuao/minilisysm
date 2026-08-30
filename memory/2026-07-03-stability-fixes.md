# 2026-07-03 长时间运行稳定性修复

## 本次完成

- 修复 `Monitor::sched_collect_loop()` 误用 `fast_collect_interval_ms` 的问题，低频采集线程现在使用 `low_freq_collect_interval_ms` 作为日志展示和 sleep deadline interval。
- `MetricRegistry` 新增 `set_gauge_family()`，支持一次刷新某个 gauge family：先清理同名 metric family 的旧 label entries，再写入本轮 samples。
- 调度延迟的动态 `pid/tid` metrics 改为按轮刷新，避免线程或进程退出后旧 label 长期留在 Prometheus 输出中。
- eBPF 调度延迟 collector 内部为 `SchedDelayCollectorRuntimeStats` 增加互斥保护，写入和 metrics scrape 读取都走快照，避免普通 `uint64_t` 字段并发读写。

## 保持不变

- 没有修改事件格式、metrics 名称、label 名称或配置 schema。
- 没有处理 JSONL fsync、Network WAL 写放大、IPv6、配置缺失告警等其他 code review 项。
- `SchedDelayCollectorRuntimeStats` 公共结构体仍保持普通字段，锁只封装在 eBPF collector 实现内部。

## 验证结果

已通过：

```bash
git -c safe.directory=E:/minilisysm diff --check
cmake -S . -B /tmp/minilisysm-build-stability -G Ninja \
  -DMINILISYSM_BUILD_TESTS=ON \
  -DMINILISYSM_BUILD_TOOLS=ON \
  -DMINILISYSM_ENABLE_EBPF=OFF
cmake --build /tmp/minilisysm-build-stability
ctest --test-dir /tmp/minilisysm-build-stability --output-on-failure -R metric_registry
ctest --test-dir /tmp/minilisysm-build-stability --output-on-failure -R sched_delay
ctest --test-dir /tmp/minilisysm-build-stability --output-on-failure -R metrics
ctest --test-dir /tmp/minilisysm-build-stability --output-on-failure
```

结果：

- 目标测试 `metric_registry`、`sched_delay_collector`、`metrics_server` 均通过。
- 全量 `15/15` CTest 通过。
- 首次全量 CTest 中 `metrics_server` 因刚跑过同一端口测试出现一次 `127.0.0.1:19108` bind 失败；等待后重跑全量通过，判断为测试端口瞬时占用，不是本次改动引入的功能失败。

## eBPF 验证边界

尝试配置 eBPF build：

```bash
cmake -S . -B /tmp/minilisysm-build-ebpf-stability -G Ninja \
  -DMINILISYSM_BUILD_TESTS=ON \
  -DMINILISYSM_BUILD_TOOLS=ON \
  -DMINILISYSM_ENABLE_EBPF=ON
```

当前 WSL 环境阻塞在 `bpftool`：

```text
bpftool was found at /usr/sbin/bpftool, but it is not runnable.
WARNING: bpftool not found for kernel 6.6.87.2-microsoft
```

需要安装匹配 WSL kernel 的 `linux-tools-6.6.87.2-microsoft-standard-WSL2` / `linux-cloud-tools-6.6.87.2-microsoft-standard-WSL2`，或设置可运行的 `BPFTOOL=/path/to/bpftool` 后再跑 `scripts/verify.sh --ebpf`。
