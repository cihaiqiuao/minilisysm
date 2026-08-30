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
| 硬件健康 | sysfs 与 EDAC | 电池、存储寿命和 ECC 状态 |

事件默认写入 JSONL 与可读摘要日志。Metrics 服务默认监听 `0.0.0.0:9108`，方便可信局域网内调试；它不内置认证或 TLS，不应直接暴露到公网。

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

也可以用浏览器打开轻量状态页：

```text
http://127.0.0.1:9108/status
http://<设备IP>:9108/status
```

状态页每 2 秒读取同一服务的 `/metrics`，展示 Agent、CPU、内存、队列、collector、sink 和硬件健康。硬件数据来自 Linux 通用节点：电池读取 `/sys/class/power_supply`，存储寿命读取 `/sys/block/*/device/life_time` 与 `pre_eol_info`，内存健康读取 EDAC ECC 计数。节点不存在时显示暂无，不额外报警。

需要限制 HTTP 客户端时，可配置精确 IPv4 白名单：

```ini
[metrics]
bind_host=0.0.0.0
allowed_clients=127.0.0.1,192.168.2.100
```

`allowed_clients` 为空时保留局域网访问；非法地址会使 MetricsServer 启动失败。该设置只是应用层过滤，不能替代防火墙、VPN 或 TLS。

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
bind_host=0.0.0.0
port=9108
allowed_clients=

[rule_runtime]
cooldown_sec=60
state_ttl_sec=3600

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

Agent 日志使用 `spdlog` 异步写入，默认同时输出到控制台和 `./logs/agent/minilisysm-agent.log`，按大小滚动：

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

将 `rotation` 改为 `daily` 可按天滚动。

### 运行时可靠性

- 告警冷却按规则和目标分别计算；Critical 升级不受冷却限制，只有已实际发布过激活事件的状态才会发布恢复事件。
- CPU、I/O 和调度规则上下文及 collector 基线会按 `state_ttl_sec` 清理，避免目标长期消失后状态无限增长。
- Monitor 与 dispatcher 的启停生命周期已串行化，重复停止幂等；采集周期等待可被停止请求立即唤醒，默认 10 秒低频周期不会拖住内部退出。
- Critical JSONL 事件总会先 flush；`fdatasync` 仍受 `max_fsync_per_minute` 限制，并分别统计成功、失败和限流。
- 网络 sink 默认关闭；启用后以 segment WAL 保存待发送事件，压缩时先发布新代再清理旧代，代际切换失败时保留旧数据，恢复语义偏向可能重复投递的 at-least-once。

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
