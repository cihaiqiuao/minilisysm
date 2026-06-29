# Ubuntu/WSL 构建与验证说明

## 基础依赖

```bash
./scripts/install_deps.sh
```

基础依赖包括：

- `build-essential`
- `cmake`
- `ninja-build`
- `pkg-config`

脚本当前只正式支持 Ubuntu/WSL 的 `apt-get` 环境。目标嵌入式 Linux 的依赖安装方式后续单独适配。

## Release 构建与测试

```bash
./scripts/verify.sh
```

该命令会执行：

- Release 配置与构建。
- 安装最终产物到 `install/`。
- 单元测试。
- `bench_spsc` benchmark。

Release 构建目录默认是 `build/release-vcpkg/`，只作为 CMake/Ninja 中间产物目录。正式安装目录统一为 `install/`，最终运行入口位于：

```bash
./scripts/run.sh
```

默认运行时读取 `install/etc/minilisysm/lisysm_monitor.ini`，并通过 `taskset` 绑定到 CPU2。需要临时切换配置或绑定其他 CPU 时：

```bash
./scripts/run.sh --config ./install/etc/minilisysm/lisysm_monitor.ini
./scripts/run.sh --cpu 1
./scripts/run.sh --cpu 0-3 --config ./my.ini
```

## ASan/UBSan 构建与测试

```bash
./scripts/build.sh --asan
./scripts/verify.sh --asan
```

ASan/UBSan 使用 `build/asan-vcpkg/` 或 `build/asan/` 作为中间构建目录，并安装到对应构建目录下的 `install/` 子目录，例如 `build/asan-vcpkg/install/`，不污染正式 `install/`。

## eBPF 可选构建与运行

```bash
./scripts/install_deps.sh --with-ebpf
./scripts/build.sh --ebpf
./scripts/verify.sh --ebpf
```

在 WSL 访问 Windows 挂载盘路径（例如 `/mnt/e/...`）时，`build/ebpf` 可能遇到 CMake `configure_file` 权限问题。脚本会自动把 eBPF 中间构建目录切到 `/tmp/minilisysm-build-ebpf`，避免 drvfs 权限噪声；最终可运行产物仍安装到项目目录下的 `install/`。

`--with-ebpf` 会额外尝试安装：

- `clang`
- `llvm`
- `bpftool`
- `libbpf-dev`
- `pkg-config`
- `linux-headers-$(uname -r)`

如果当前内核 headers 包不可用，脚本会提示 warning，但不阻断基础构建。

eBPF 构建开启后，CMake 会执行：

- 从 `/sys/kernel/btf/vmlinux` 生成 `vmlinux.h`。
- 使用 `clang -target bpf` 编译 `ebpf/bpf/sched_delay.bpf.c`。
- 使用 `bpftool gen skeleton` 生成 `sched_delay.skel.h`。
- 编译 `EbpfSchedDelayCollector` 并链接 libbpf。

非 root 执行 `scripts/verify.sh --ebpf` 时，只验证构建和单测。root/sudo 执行时会额外运行 eBPF smoke：

```bash
sudo bash scripts/verify.sh --ebpf
```

手动运行 eBPF 数据源时，把配置中的 `[sched_delay_rule] source` 改为 `ebpf`，然后使用 root/sudo 启动：

```bash
sudo ./install/bin/minilisysm
```

如果 eBPF 初始化失败，程序会自动回退 `/proc` 调度延迟 collector，主监控链路不会崩溃。

## 运行 smoke

```bash
timeout 3s ./scripts/run.sh || test $? -eq 124
test -n "$(find logs/events/jsonl -name '*.jsonl' -print -quit)"
test -n "$(find logs/events/summary -name '*.summary.log' -print -quit)"
```

默认运行产物目录是 `./logs`。Agent 运行日志在 `./logs/agent`，事件缓存目录是 `./logs/events`，网络 WAL 目录是 `./logs/wal`。

持久化目录中会同时生成两类文件：

```text
jsonl/minilisysm-events-20260629-111101-p6750-part000001.jsonl
summary/minilisysm-events-20260629-111101-p6750-part000001.summary.log
```

`jsonl/` 中是一行一个完整 JSON 事件，给程序和上传链路使用。`summary/` 中是面向人工排查的短摘要块，默认不写 ANSI 控制字符，方便在编辑器里直接查看。

需要关闭摘要或在终端里开启颜色时，可以调整：

```ini
[persistence]
summary_enable=true
summary_color=false
```

## systemd 开机自启动

安装服务前先完成 Release 构建和安装：

```bash
./scripts/build.sh
```

安装并立即启动后台服务：

```bash
./scripts/install_service.sh
```

默认服务名是 `minilisysm.service`，默认以当前用户运行，并通过 `scripts/run.sh` 绑定到 CPU2。需要指定 CPU 或配置文件时：

```bash
./scripts/install_service.sh --cpu 1
./scripts/install_service.sh --config ./install/etc/minilisysm/lisysm_monitor.ini
```

查看状态和日志：

```bash
systemctl status minilisysm.service
journalctl -u minilisysm.service -f
```

停止、重启、禁用：

```bash
sudo systemctl stop minilisysm.service
sudo systemctl restart minilisysm.service
sudo systemctl disable --now minilisysm.service
```

移除服务文件：

```bash
./scripts/uninstall_service.sh
```

## I/O 堵塞检测配置

默认配置启用 `[io_delay_rule]`，通过 `/proc/diskstats` 采集块设备 I/O 状态，不需要额外依赖。

```ini
[io_delay_rule]
enable=true
device_whitelist=
await_warning_ms=50
await_critical_ms=200
await_recovery_ms=20
util_warning_percent=80
util_critical_percent=95
util_recovery_percent=50
continuous_warning_windows=3
continuous_critical_windows=2
recovery_windows=3
max_targets=16
```

`device_whitelist` 为空时会跳过 `loop`、`ram`、`fd` 这类虚拟设备，并扫描其他块设备。需要只监控指定设备时，可以写成：

```ini
device_whitelist=sda,nvme0n1
```
