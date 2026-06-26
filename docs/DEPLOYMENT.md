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
- 单元测试。
- `bench_spsc` benchmark。

## ASan/UBSan 构建与测试

```bash
./scripts/build.sh --asan
./scripts/verify.sh --asan
```

ASan/UBSan 使用独立 `build-asan` 目录，不与 Release 构建混用。

## eBPF 可选构建与运行

```bash
./scripts/install_deps.sh --with-ebpf
./scripts/build.sh --ebpf
./scripts/verify.sh --ebpf
```

在 WSL 访问 Windows 挂载盘路径（例如 `/mnt/e/...`）时，`build-ebpf` 可能遇到 CMake `configure_file` 权限问题。脚本会自动把 eBPF 构建目录切到 `/tmp/minilisysm-build-ebpf`，避免 drvfs 权限噪声。

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
sudo /tmp/minilisysm-build-ebpf/minilisysm configs/lisysm_monitor.ini
```

如果 eBPF 初始化失败，程序会自动回退 `/proc` 调度延迟 collector，主监控链路不会崩溃。

## 运行 smoke

```bash
timeout 3s ./build/minilisysm configs/lisysm_monitor.ini || test $? -eq 124
test -f lisysm_events/events_000001.jsonl
```

默认事件缓存目录是 `./lisysm_events`。

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
