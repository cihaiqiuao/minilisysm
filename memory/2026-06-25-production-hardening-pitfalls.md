# 2026-06-25 生产化增强过程中的问题与修复

## 背景

生产化增强涉及 Metrics、NetworkSink/WAL、eBPF 生命周期、eBPF 聚合、RuleEngine 模板化和测试补充。改动跨度较大，过程中暴露了一些接口、编译和运行验证问题。本记录用于保存踩坑和解决方式，方便后续继续迭代时快速定位。

## 头文件可见性问题

### 问题

`Monitor` 头文件中新增：

```cpp
void record_self_status_metrics(const SelfStatusSample& sample);
```

但 `monitor.hpp` 没有包含 `self_status_collector.hpp`，导致编译时报：

```text
SelfStatusSample does not name a type
```

### 解决

在 `monitor.hpp` 中包含 `minilisysm/collectors/self_status_collector.hpp`，让 `SelfStatusSample` 类型在声明处可见。

## MetricLabel initializer list 问题

### 问题

代码中使用：

```cpp
std::vector<MetricLabel> label{{"sink", item.first}};
```

但 `MetricLabel` 只有默认聚合字段，在部分编译路径下 initializer list 推导失败。

### 解决

给 `MetricLabel` 增加显式构造函数：

```cpp
MetricLabel(std::string key_value, std::string label_value);
```

并包含 `<utility>`，内部使用 `std::move`。

## sink_stats() 放错 class

### 问题

为了让 `/metrics` 按 sink 暴露统计，新增 `sink_stats()`。第一次声明放到了单个 `EventDispatcher` 上，但实现写在 `EventDispatcherGroup` 上，导致编译报：

```text
no declaration matches EventDispatcherGroup::sink_stats() const
```

### 解决

将 `sink_stats()` 声明移动到 `EventDispatcherGroup`，因为只有 group 持有 `sinks_`，单个 dispatcher 只知道 sink queue 指针，不知道 sink 名称和完整统计。

## eBPF exit 清理 pid/tid 修正

### 问题

`sched_process_exit` 初版只使用 tracepoint 中的 pid 字段清理 map，容易混淆 pid/tid：

- Linux 调度层面很多字段实际是 task id，也就是 tid。
- allowlist 中 pid map 和 tid map 分别表达进程和线程。

### 解决

在 BPF 程序中使用：

```c
bpf_get_current_pid_tgid()
```

拆出：

- tgid 作为 pid。
- pid_tgid 低 32 位作为 tid。

然后分别清理：

- `pid_allowlist`
- `tid_allowlist`
- `runnable_since`

## NetworkSink WAL 语义收口

### 问题

第一版 WAL 是单文件 `events.wal`，只能表达“失败后保留”，但不够清晰：

- 不支持 segment 滚动。
- 不支持容量边界。
- 重启恢复和 ack 语义不够直观。

### 解决

改为 segment WAL：

- 文件名：`events_000001.wal`
- 启动时扫描所有 `.wal`
- 只加载未 ack 记录
- 2xx 后按 batch ack 并重写 WAL
- WAL 超限时丢弃非 Critical，并统计 `wal_overflow_dropped_events`

## 网络连接超时

### 问题

配置里已有：

```ini
connect_timeout_ms=500
request_timeout_ms=2000
```

但基础版只使用了 send/recv timeout，没有真正控制 connect 阶段。

### 解决

将 connect 改为：

1. socket 设为 non-blocking。
2. 调用 `connect()`。
3. 如果返回 `EINPROGRESS`，使用 `poll(POLLOUT)` 等待 `connect_timeout_ms`。
4. 用 `getsockopt(SO_ERROR)` 判断连接结果。
5. 连接成功后恢复原 flags。

## eBPF skeleton 验证

### 问题

eBPF 结构体和 tracepoint 变更后，普通 Release 构建无法发现 BPF 编译问题，必须跑 `--ebpf`。

### 解决

每次修改 `sched_delay.bpf.c` 后执行：

```bash
bash scripts/verify.sh --ebpf
```

验证：

- `vmlinux.h` 生成。
- BPF object 编译。
- `bpftool gen skeleton`。
- 用户态 collector 链接。
- root smoke 写出 JSONL。

## WSL 路径与 Git ownership 问题

### 问题

Windows/PowerShell 下直接访问 Git 状态时出现：

```text
fatal: detected dubious ownership in repository
```

WSL 有时还会输出：

```text
wsl: Failed to translate 'E:\minilisysm'
```

### 解决

验证命令统一使用稳定模式：

```powershell
wsl -u root bash -lc 'mountpoint -q /mnt/e || mount -t drvfs E: /mnt/e; cd /mnt/e/minilisysm && ...'
```

读取 Git 状态时使用临时参数：

```bash
git -c safe.directory=/mnt/e/minilisysm status --short
```

不修改全局 Git 配置。

## 验证结果

最终三套验证均通过：

```bash
bash scripts/verify.sh
bash scripts/verify.sh --asan
bash scripts/verify.sh --ebpf
```

结果：

- Release：9/9 单测通过，benchmark 通过。
- ASan/UBSan：9/9 单测通过。
- eBPF：skeleton 生成通过，9/9 单测通过，root smoke 写出 JSONL。
