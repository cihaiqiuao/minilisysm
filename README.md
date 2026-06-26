# minilisysm

`minilisysm` 是 Lisysm Linux 稳定性监控链路的工程化初版。当前结构按守护进程入口、核心库、独立 eBPF 模块、测试、工具和部署脚本拆分，默认面向 Ubuntu/WSL 构建验证。

当前已实现的规则链路：

- 系统内存压力：基于 `/proc/meminfo` 的 `MemAvailable` 判断 Warning、Critical 和 Recovery。
- 监控自身 RSS 保护：基于 `/proc/self/status` 的 `VmRSS` 判断监控模块自身内存压力。
- 队列压力保护：基于 SPSC 队列 depth、drop count 和容量百分比判断 `monitor_queue_pressure`。
- 调度延迟趋势：默认基于 `/proc/<pid>/task/<tid>/sched` 判断 `sched_delay_risk`；启用 eBPF 构建并配置 `source=ebpf` 后，可通过 `sched_switch` tracepoint 和 libbpf ring buffer 采集调度等待时间。
- I/O 堵塞检测：基于 `/proc/diskstats` 对块设备 I/O 完成数、读写耗时和忙碌时间做差分，判断 `io_delay_risk`。

## 目录结构

```text
apps/minilisysm-agent/      守护进程入口
include/minilisysm/         核心库公共头文件
src/                        核心库实现
ebpf/                       可选 eBPF 用户态 loader 和 BPF 程序
configs/                    默认运行配置
tests/unit/                 单元测试
tools/bench/                benchmark 工具
scripts/                    依赖安装、构建和验证脚本
cmake/                      CMake 选项和编译器设置
docs/                       架构、部署和 eBPF 说明
memory/                     项目交接记录
```

## 一键验证

Ubuntu/WSL 环境安装基础依赖：

```bash
./scripts/install_deps.sh
```

构建并验证 Release：

```bash
./scripts/verify.sh
```

ASan/UBSan 验证：

```bash
./scripts/build.sh --asan
./scripts/verify.sh --asan
```

可选 eBPF 构建验证：

```bash
./scripts/install_deps.sh --with-ebpf
./scripts/build.sh --ebpf
./scripts/verify.sh --ebpf
```

eBPF 运行需要 root/sudo 权限：

```bash
sudo /tmp/minilisysm-build-ebpf/minilisysm configs/lisysm_monitor.ini
```

## 运行

```bash
./build/minilisysm configs/lisysm_monitor.ini
```

默认缓存目录为 `./lisysm_events`，事件文件按大小滚动为 `events_000001.jsonl`。

## 架构要点

- 快速路径不做网络请求、不做 JSON 序列化、不等待后台消费者。
- 事件对象定长，SPSC 队列容量固定，队列满时记录丢弃统计并返回。
- `fast_collector` 和 `sched_collector` 各自拥有独立 SPSC 队列。
- 调度延迟和 I/O 堵塞共用低优先级 `sched_collector` 线程，避免额外增加采集线程数量。
- `EventDispatcherGroup` 为每条采集队列创建独立 dispatcher，并为每个 sink 分配专属 SPSC 输入队列；当前 `JsonlEventSink` 负责 JSONL buffer 复用和滚动落盘。
- eBPF 作为可选增强数据源，加载失败时回退 `/proc` collector，规则判断仍复用 `RuleEngine::evaluate_sched_delay()`。

更多说明见：

- `docs/ARCHITECTURE.md`
- `docs/DEPLOYMENT.md`
- `docs/EBPF.md`
