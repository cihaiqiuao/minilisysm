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

项目的 C++ 第三方依赖由 vcpkg manifest 管理，当前包含 Agent 日志库 `spdlog`。`scripts/build.sh` 和 `scripts/verify.sh` 默认会自动引导 vcpkg；如果目标环境已经通过系统包提供 C++ 依赖，可以显式使用 `--no-vcpkg`。

ASan/UBSan 验证：

```bash
./scripts/build.sh --asan
./scripts/verify.sh --asan
```

格式化、静态检查和覆盖率报告：

```bash
./scripts/format_check.sh
./scripts/lint.sh
./scripts/coverage.sh
```

`scripts/lint.sh` 依赖 `build/release/compile_commands.json`，如果不存在会自动配置 Release 构建目录。覆盖率报告输出到 `build/coverage-report/index.html`。

可选 eBPF 构建验证：

```bash
./scripts/install_deps.sh --with-ebpf
./scripts/build.sh --ebpf
./scripts/verify.sh --ebpf
```

eBPF 运行需要 root/sudo 权限：

```bash
sudo ./install/bin/minilisysm
```

## 运行

```bash
./scripts/run.sh
```

默认所有运行产物统一放在 `./logs/` 下。Agent 运行日志写入 `agent/`，事件缓存写入 `events/`，网络 WAL 写入 `wal/`。机器可读事件写入 `events/jsonl/` 子目录，人工排查摘要写入 `events/summary/` 子目录。文件名包含启动时间、进程号和滚动段号，例如：

```text
logs/events/jsonl/minilisysm-events-20260629-111101-p6750-part000001.jsonl
logs/events/summary/minilisysm-events-20260629-111101-p6750-part000001.summary.log
```

摘要日志默认不写 ANSI 控制字符，方便在编辑器里直接查看；需要在终端里看颜色时，可以把配置里的 `summary_color` 改成 `true` 后用 `tail -f logs/events/summary/*.summary.log` 查看。

Agent 自身运行日志使用 `spdlog` 异步写入，默认同时输出到控制台和 `./logs/agent/minilisysm-agent.log`，支持 `debug`、`info`、`warn`、`error` 分级。默认按大小滚动，避免长期运行打满磁盘：

```ini
[agent_log]
enable=true
level=info
console=true
path=./logs/agent/minilisysm-agent.log
rotation=size
rotate_mb=16
rotate_files=8
async_queue_size=8192
```

需要按天滚动时，将 `rotation` 改为 `daily`。

## 构建产物布局

`build/` 只作为 CMake/Ninja 中间构建目录，默认 vcpkg Release 构建位于 `build/release-vcpkg/`。正式可运行和可分发产物统一安装到 `install/`，不再按 vcpkg/no-vcpkg 拆出多份根目录：

```text
install/bin/minilisysm                    主程序
install/etc/minilisysm/lisysm_monitor.ini 默认配置
install/share/doc/minilisysm/             文档
```

默认情况下，`install/bin/minilisysm` 会读取 `install/etc/minilisysm/lisysm_monitor.ini`。需要临时使用其他配置时，可以把配置路径作为第一个参数传入。

`scripts/run.sh` 默认通过 `taskset` 把进程绑定到 CPU2。也可以通过运行脚本指定配置和其他 CPU：

```bash
./scripts/run.sh --config ./install/etc/minilisysm/lisysm_monitor.ini
./scripts/run.sh --cpu 1
./scripts/run.sh --cpu 0-3 --config ./my.ini
```

`--vcpkg` 和 `--no-vcpkg` 只表示依赖来源，不改变正式安装目录。`--ebpf` 会生成 eBPF 能力的正式产物并覆盖 `install/`。`--asan` 是测试产物，安装在 `build/asan-vcpkg/install/` 或 `build/asan/install/`，避免污染正式运行目录。

## 开机自启动

使用 systemd 安装后台服务：

```bash
./scripts/install_service.sh
```

查看状态和日志：

```bash
systemctl status minilisysm.service
journalctl -u minilisysm.service -f
```

停用并移除服务：

```bash
./scripts/uninstall_service.sh
```

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
