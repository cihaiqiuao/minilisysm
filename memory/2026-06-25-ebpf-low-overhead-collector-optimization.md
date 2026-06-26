# 2026-06-25 eBPF 与采集链路低开销优化

## 背景

最初 eBPF 调度延迟采集已经能跑通，但存在几个生产化风险：

- `sched_switch` 触发频率很高，直接向 ringbuf 上报可能造成事件风暴。
- 用户态如果对每条 eBPF 事件都读取 `/proc/<pid>/comm` 做白名单匹配，会造成严重 syscall 和 I/O 开销。
- `/proc` fallback 每轮全量扫描进程和线程，在线程数很多时成本较高。
- 采集循环使用 `sleep_for(interval)` 会把采集耗时叠加进周期，长期运行产生定时漂移。
- dispatcher/sink 空队列等待固定 20ms，低负载下事件分发延迟偏高且不可配置。

## eBPF 侧优化

### 阈值过滤

BPF 程序增加 `min_wait_ns` 配置。只有调度等待超过阈值的事件才进入 ringbuf。

默认映射：

```ini
ebpf_min_wait_us=10000
```

如果未配置，则默认使用 `sched_wait_sum_warning_us`。

### pid/tid 白名单映射

BPF 侧增加：

- `pid_allowlist`
- `tid_allowlist`

用户态周期扫描 `/proc`，把配置中的 `sched_process_whitelist`、`sched_thread_whitelist` 转换为具体 pid/tid，然后写入 BPF map。

这样 ringbuf 热路径不再需要用户态逐条打开 `/proc/<pid>/comm`。

### ringbuf 丢弃计数器

BPF 侧增加 counter map。当 `bpf_ringbuf_reserve()` 失败时，不再只是静默丢弃，而是累加 `ringbuf_drops`。

用户态周期读取 counter delta，并进入 collector failure / pressure 统计。

### 生命周期 counter

后续补强中增加：

- `allowlist_exec_seen`
- `allowlist_exit_cleaned`
- `allowlist_stale_hits`
- `aggregate_drops`

其中 `allowlist_exec_seen` 用于触发用户态快速刷新 allowlist，`allowlist_stale_hits` 用于观测白名单过期或 PID/TID 复用风险。

## 用户态 eBPF collector 优化

### ringbuf callback 不读 `/proc`

旧风险：

```cpp
if (!owner->accepts(sample.pid, sample.tid)) return;
```

如果 `accepts()` 内部读取 `/proc/<pid>/comm`，那么每条 ringbuf 事件都会触发文件打开和读取。高频上下文切换下，这会造成明显 CPU 和 I/O 开销。

现在的设计：

- 用户态只在初始化和低频刷新时扫描 `/proc`。
- 扫描结果写入 BPF pid/tid allowlist map。
- BPF 在内核态先过滤。
- ringbuf callback 只做轻量转换：
  - BPF event -> `SchedDelaySample`
  - 不打开文件
  - 不做字符串匹配

### 单次 poll 事件上限

用户态使用 `sched_ebpf_max_events_per_poll` 限制一次 poll 最多消费的事件数，避免 ringbuf 积压导致单轮采集时间过长。

### allowlist 快速刷新

后续补强中，BPF `sched_process_exec` 只记录 exec counter，不做字符串匹配。用户态每次 poll 读取 counter delta：

- 如果发现 exec delta。
- 且配置了 process/thread whitelist。
- 立即刷新 `/proc` allowlist。

这样可以比固定 10 秒刷新周期更快发现新进程或新线程。

## `/proc` fallback 优化

`SchedDelayCollector` 增加 comm cache：

- `process_comm_cache_`
- `thread_comm_cache_`

缓存按 `proc_cache_refresh_ms` 刷新，不再每轮重复读取所有 comm。

新增配置：

```ini
proc_cache_refresh_ms=10000
proc_max_scan_threads=4096
collector_overrun_warning_ms=200
```

优化点：

- 配置白名单时优先匹配缓存结果。
- 白名单为空时仍支持全系统扫描，但用 `proc_max_scan_threads` 限制极端线程数。
- 采集耗时超过 `collector_overrun_warning_ms` 时增加 failure/overrun 统计。

## 定时器与空队列等待优化

采集循环从：

```cpp
sleep_for(interval)
```

改成 deadline 模式：

```cpp
next_deadline += interval;
sleep_until(next_deadline);
```

如果采集耗时超过周期，会校正 deadline，避免长期漂移。

dispatcher 和 sink 的空队列等待变为配置项：

```ini
[runtime]
dispatcher_idle_sleep_ms=2
sink_idle_sleep_ms=5
```

这样低负载下事件延迟更低，同时仍保持非阻塞轮询模型。

## 验证

相关改动经过以下验证：

```bash
bash scripts/verify.sh
bash scripts/verify.sh --asan
bash scripts/verify.sh --ebpf
```

验证内容包括：

- Release 构建和单测。
- ASan/UBSan。
- eBPF skeleton 生成。
- eBPF root smoke 写出 JSONL。

## 当前边界

- eBPF 不在内核态做 comm 字符串匹配，避免 BPF 程序复杂化。
- pid/tid allowlist 仍依赖用户态扫描 `/proc` 维护。
- exec counter 可以加速刷新，但不是严格的内核态自动发现。
- 聚合默认关闭，适合高上下文切换压力场景按需打开。
