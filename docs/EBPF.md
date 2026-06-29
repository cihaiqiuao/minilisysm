# eBPF 调度延迟采集说明

当前 eBPF 接入是可选增强数据源，默认关闭，不影响现有 `/proc` 调度延迟采集链路。启用 `MINILISYSM_ENABLE_EBPF=ON` 后，构建系统会生成 CO-RE skeleton，用户态 collector 会加载 `sched_switch` tracepoint，并通过 libbpf ring buffer 读取调度等待事件。

## 当前状态

- CMake 开关：`MINILISYSM_ENABLE_EBPF`，默认 `OFF`。
- 配置项：`[sched_delay_rule] source=proc|ebpf`，默认 `proc`。
- 统一接口：`SchedDelayCollectorInterface`。
- 默认实现：`SchedDelayCollector`，读取 `/proc/<pid>/task/<tid>/sched`。
- 可选实现：`EbpfSchedDelayCollector`，位于 `ebpf/include/` 和 `ebpf/src/`。
- BPF 程序：`ebpf/bpf/sched_delay.bpf.c`，挂载 `tracepoint/sched/sched_switch`。
- BPF 构建产物：`vmlinux.h`、`sched_delay.bpf.o`、`sched_delay.skel.h`，生成到 eBPF 构建目录的 `generated/ebpf/`。
- 运行策略：`source=ebpf` 初始化失败时自动回退 `/proc` collector，并通过 collector failure 统计暴露失败。

## 启用方式

安装可选 eBPF 依赖：

```bash
./scripts/install_deps.sh --with-ebpf
```

构建并验证 eBPF 用户态 loader：

```bash
./scripts/build.sh --ebpf
./scripts/verify.sh --ebpf
```

配置文件中选择 eBPF 数据源：

```ini
[sched_delay_rule]
enable=true
source=ebpf
```

运行 eBPF 版本需要 root/sudo 权限：

```bash
sudo ./install/bin/minilisysm
```

在 WSL 的 `/mnt/e/...` 这类 Windows 挂载盘路径下，`scripts/build.sh --ebpf` 会自动把中间构建目录切到 `/tmp/minilisysm-build-ebpf`。最终可运行产物仍安装到项目目录下的 `install/`。

## 采集语义

- BPF 程序在 `sched_switch` 中记录被切出且仍处于 runnable 状态的线程。
- 该线程下一次被切入 CPU 时，计算等待时间 `delta_wait_ns`。
- ring buffer 输出 `{pid, tid, delta_wait_ns, involuntary_switches}`。
- 用户态把 `delta_wait_ns / 1000` 转换为 `SchedDelaySample::delta_wait_sum_us`。
- 用户态继续复用现有 `sched_process_whitelist` 和 `sched_thread_whitelist`，避免第一版 BPF 程序承担过多过滤逻辑。
- 转换后的样本继续走 `RuleEngine::evaluate_sched_delay()`，不会复制第二套规则状态机。

## 已知限制

- 第一版只支持 CO-RE/libbpf/bpftool skeleton 路线。
- 加载 BPF 程序需要 root/sudo 权限，以及可用的 `/sys/kernel/btf/vmlinux`。
- WSL 的 `bpftool` 包可能不可直接安装；脚本会尝试使用 `linux-tools-*` 中可执行的 `bpftool`，也可以通过 `BPFTOOL=/path/to/bpftool` 显式指定。
- ring buffer 满时允许丢事件，监控主流程不会因此阻塞。
- 默认阈值较高时，运行 smoke 不一定触发 `sched_delay_risk`，但应至少能写出 `monitor_started`。

## 设计约束

- eBPF 只作为增强数据源，不能替代当前 `/proc` fallback。
- 快速路径仍然只产出固定大小 `InternalEvent`，不在采集线程中做网络请求。
- 规则判断继续复用 `RuleEngine::evaluate_sched_delay()`，避免为 eBPF 单独复制一套状态机。
