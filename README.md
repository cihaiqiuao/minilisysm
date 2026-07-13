# minilisysm

`minilisysm` 是面向 Linux 边缘设备的轻量稳定性监控服务。它定期采集系统资源、I/O、队列与指定进程状态，按阈值生成结构化事件，并通过 Prometheus `/metrics` 与终端工具 `qalog` 提供实时可观测性。

适用场景包括 RK3588S、工控机和 Ubuntu/WSL 环境中长期运行的视觉、推流或网关服务。

## 能力概览

| 范围 | 数据源 | 能力 |
| --- | --- | --- |
| 系统内存 | `/proc/meminfo` | 可用内存阈值告警与恢复 |
| CPU | `/proc/stat` | 整机或指定核心的使用率风险告警 |
| 磁盘 I/O | `/proc/diskstats` | `await`、利用率与 I/O 完成量监控 |
| 内部队列 | SPSC 队列状态 | 深度、丢弃和高水位保护 |
| 监控器自身 | `/proc/self/status` | RSS 自保护 |
| 白名单进程 | `/proc/<pid>` | 在线状态、CPU、RSS、线程数 |
| 调度等待 | `/proc/.../sched` 或可选 eBPF | 调度延迟趋势与事件 |

事件默认写入 JSONL 与可读摘要日志；指标默认仅绑定到本机 `127.0.0.1:9108`。

## 快速开始

### 1. 安装依赖并验证

```bash
git clone https://github.com/cihaiqiuao/minilisysm.git
cd minilisysm

./scripts/install_deps.sh
./scripts/verify.sh
```

项目默认使用 vcpkg 管理 C++ 依赖。若系统已具备依赖，可改用：

```bash
./scripts/verify.sh --no-vcpkg
```

验证会构建 Release 程序、运行 CTest，并执行 SPSC 基准测试。

### 2. 启动监控器

```bash
./scripts/run.sh
```

默认配置文件为：

```text
install/etc/minilisysm/lisysm_monitor.ini
```

使用单独的板端配置，避免后续安装覆盖运行参数：

```bash
cp configs/lisysm_monitor.ini /userdata/minilisysm.ini
./scripts/run.sh --config /userdata/minilisysm.ini
```

### 3. 查看实时状态

```bash
./scripts/qalog
./scripts/qalog -f

curl -s http://127.0.0.1:9108/metrics
```

安装后的 `install/bin/qalog` 在 `PATH` 中时可直接运行 `qalog`。

## 监控业务进程

在运行配置的 `[sched_delay_rule]` 中设置进程名白名单：

```ini
[sched_delay_rule]
process_whitelist=train_stability
```

进程名匹配 Linux 的 `/proc/<pid>/comm`。配置多个目标时使用逗号分隔：

```ini
process_whitelist=train_stability,another_service
```

`qalog` 将增加以下区块：

```text
Whitelisted processes
  train_stability      up=yes
    pid=101383  cpu=23.00% rss=85.4 MiB threads=12
```

- `up`：是否存在匹配进程。
- `cpu`：一个采样周期内的单核 CPU 百分比；多线程进程可以超过 `100%`。
- `rss`：进程常驻物理内存。
- `threads`：进程线程数。

同时会导出以下 Prometheus 指标：

```text
minilisysm_whitelisted_process_up{process}
minilisysm_whitelisted_process_cpu_usage_percent{process,pid}
minilisysm_whitelisted_process_rss_bytes{process,pid}
minilisysm_whitelisted_process_threads{process,pid}
```

未启动的白名单进程不会报警。进程运行期间，监控器会以滚动窗口判断 RSS 净增长，默认 10 分钟增长 100 MiB 发 Warning、200 MiB 发 Critical，增长回落到 20 MiB 以下后发恢复事件：

```ini
[process_memory_rule]
enable=true
growth_warning_mb=100
growth_critical_mb=200
growth_recovery_mb=20
growth_window_sec=600
```

事件类型为 `whitelisted_process_memory_risk`。

## 关键配置

配置模板在 [`configs/lisysm_monitor.ini`](configs/lisysm_monitor.ini)。常用项如下：

```ini
[linux_stability_monitor]
fast_collect_interval_ms=1000

[metrics]
bind_host=127.0.0.1
port=9108

[memory_rule]
mem_available_warning_mb=512
mem_available_critical_mb=256

[cpu_usage_rule]
mode=total
warning_percent=80
critical_percent=95

[io_delay_rule]
await_warning_ms=50
await_critical_ms=200

[sched_delay_rule]
source=proc
process_whitelist=
```

日志和事件默认位于 `./logs/`：

```text
logs/agent/                  Agent 运行日志
logs/events/jsonl/           机器可读事件
logs/events/summary/         人工排查摘要
logs/wal/                    网络 sink 的待发送数据
```

## systemd 部署

构建后安装为开机自启服务：

```bash
./scripts/install_service.sh \
  --user "$(id -un)" \
  --config /userdata/minilisysm.ini
```

查看状态与日志：

```bash
systemctl status minilisysm.service
journalctl -u minilisysm.service -f
```

移除服务：

```bash
./scripts/uninstall_service.sh
```

## 调度延迟与 eBPF

默认 `source=proc` 读取 `/proc/<pid>/task/<tid>/sched`。部分定制内核可能没有 `se.statistics.wait_sum` 字段；在这种内核上，白名单进程的 CPU、RSS、线程数仍可展示，但无法获得真实的调度等待样本。

具备 BTF 的内核可启用 eBPF 数据源：

```bash
./scripts/install_deps.sh --with-ebpf
./scripts/build.sh --ebpf
sudo ./install/bin/minilisysm
```

并在配置中设置：

```ini
[sched_delay_rule]
source=ebpf
```

eBPF 需要 `/sys/kernel/btf/vmlinux`、可用的 `bpftool`/`libbpf`，以及 root 或相应 BPF 权限。详细说明见 [`docs/EBPF.md`](docs/EBPF.md)。

## 开发与质量检查

```bash
# AddressSanitizer / UndefinedBehaviorSanitizer
./scripts/verify.sh --asan

# 格式检查、静态检查、覆盖率
./scripts/format_check.sh
./scripts/lint.sh
./scripts/coverage.sh
```

## 项目结构

```text
apps/       守护进程入口
src/        核心实现与采集器
include/    公共头文件
configs/    默认配置模板
scripts/    构建、验证、运行和 systemd 脚本
docs/       架构、部署与 eBPF 文档
tests/      单元测试
memory/     调试与交接记录
```

更多设计细节参见 [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) 与 [`docs/DEPLOYMENT.md`](docs/DEPLOYMENT.md)。
