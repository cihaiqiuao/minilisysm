# 2026-06-24 eBPF 调度延迟采集落地

## 已完成

- 增加 `MINILISYSM_ENABLE_EBPF` CMake 开关，默认关闭，避免未安装 libbpf/bpftool 的环境构建失败。
- 在 `[sched_delay_rule]` 中增加 `source=proc|ebpf` 配置项，默认继续使用 `proc`。
- 新增 `SchedDelayCollectorInterface`，让 `Monitor` 通过统一接口持有调度延迟 collector。
- 保留现有 `/proc/<pid>/task/<tid>/sched` 实现作为默认 fallback。
- 新增 `EbpfSchedDelayCollector`，启用 eBPF 编译并配置 `source=ebpf` 时由 `CollectorFactory` 创建。
- CMake 在 `MINILISYSM_ENABLE_EBPF=ON` 时生成 `vmlinux.h`、编译 `sched_delay.bpf.o`、调用 `bpftool gen skeleton` 生成 `sched_delay.skel.h`。
- 新增 `ebpf/bpf/sched_delay.bpf.c`，基于 `sched_switch` tracepoint 记录 runnable 线程等待时间。
- 用户态通过 libbpf 加载、attach、poll ring buffer，并转换为现有 `SchedDelaySample`。
- eBPF 初始化失败时自动回退 `/proc` collector，并通过 collector failure 计数暴露失败。
- 新增 `docs/EBPF.md`，记录启用方式、运行权限、采集语义和限制。

## 设计说明

- 默认配置仍走 `/proc`，保证现有构建、测试和运行链路不受 eBPF 外部依赖影响。
- eBPF 采样结果转换为现有 `SchedDelaySample`，继续复用 `RuleEngine::evaluate_sched_delay()`。
- 第一版不过度做内核侧过滤，进程和线程白名单继续放在用户态处理。
- WSL `/mnt/e/...` 路径下 eBPF 构建目录自动使用 `/tmp/minilisysm-build-ebpf`。

## 已验证

1. `bash scripts/build.sh --ebpf` 能生成 BPF object、skeleton 和最终可执行文件。
2. `bash scripts/verify.sh --ebpf` 在 root 下通过当前单测集，并写出 eBPF smoke JSONL。
3. 低阈值调度压力 smoke 已触发 `sched_delay_risk`，证明 ring buffer 事件进入现有规则链路。
4. 默认 `source=proc` 行为保持不变。
