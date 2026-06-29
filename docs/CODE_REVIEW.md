# minilisysm 代码审查报告

> **审查日期**: 2026-06-29
> **审查范围**: 完整代码库（核心库、采集器、规则引擎、事件分发、存储、eBPF 模块、测试）
> **代码库版本**: main 分支（工程第一版）

---

## 严重性分级说明

| 级别 | 含义 |
|------|------|
| 🔴 Critical | 可能导致 crash、数据丢失或安全漏洞，必须修复 |
| 🟠 High | 功能性缺陷，特定条件下导致错误行为 |
| 🟡 Medium | 设计或健壮性问题，在异常条件下可能故障 |
| 🔵 Low | 风格、命名、文档等可维护性问题 |

---

## 🔴 Critical

### C1. MetricRegistry 指标无限增长（OOM 风险）

**文件**: `src/runtime/metric_registry.cpp:96-112`、`src/runtime/monitor.cpp:407-408`

**问题描述**: `record_sched_delay_metrics()` 使用 PID/TID 作为 Prometheus label 键向 `MetricRegistry` 注册指标：

```cpp
// monitor.cpp:407
metrics_.set_gauge("minilisysm_sched_wait_us",
    static_cast<double>(sample.delta_wait_sum_us),
    {{"pid", pid}, {"tid", tid}});
```

在 `MetricRegistry::set_locked()` 中，每次有新 PID/TID 组合出现，就向 `std::unordered_map` 插入新条目。**但该 map 从不清理旧条目**。运行在 PID 不断创建和销毁的系统（例如容器频繁重启、短生命周期进程多的服务器）上时，map 中的条目数量持续增长。

PID 虽然在 Linux 上会回收，但 **map key 不会覆盖**——同一 PID 的新进程只会创建新条目（因为 label 字符串不同）。

**影响**:
- 长期运行后内存持续增长，可能导致 OOM
- 在 Prometheus scrape 时产生大量废弃指标

**建议**:
1. 限制 label 基数，例如只记录前 N 个最活跃的 PID/TID
2. 对已退出进程的指标设置 TTL，超时后清理
3. 改用聚合模式（按进程名聚合）而非精确记录每个 PID/TID

---

### C2. SchedDelayCollector 达到扫描上限误报 CollectorFailure

**文件**: `src/collectors/sched_delay_collector.cpp:119-131`

**问题描述**: 达到配置的 `sched_proc_max_scan_threads` 上限时，`last_failure_count_` **无条件**递增一次。这不是真正的 IO 错误或采集失败——只是达到了用户在配置中设定的扫描上限，是预期行为。

```cpp
// sched_delay_collector.cpp:124
if (scanned_threads >= config_.sched_proc_max_scan_threads) {
    ++last_failure_count_;  // ← 这不是真正的失败
    // ... 返回 samples
}
```

**影响**: 每次扫描达到 `sched_proc_max_scan_threads` 上限（配置默认 4096，但如果用户设低则每个周期都达到），都会在 `monitor.cpp:198-204` 处触发 `CollectorFailure` 告警事件，产生大量噪音告警。

**建议**: 达到配置上限不应计为失败；可将"已满扫描"和"IO 错误"的状态分开跟踪。

---

### C3. SchedDelayCollectorRuntimeStats 存在数据竞争（未定义行为）

**文件**: `ebpf/src/ebpf_sched_delay_collector.cpp:226-232`、`src/runtime/monitor.cpp:369-378`

**问题描述**: `Impl::runtime_stats` 是一个纯结构体（无原子字段、无互斥锁），在两个线程之间并发读写：

- **sched_collector 线程**: `poll()` → `read_counters()` → 写入 `runtime_stats` 字段
- **MetricsServer HTTP 线程**: 通过 `runtime_stats()` 读取

```cpp
// 写入线程（sched_collector 循环）
runtime_stats.ebpf_ringbuf_drops = counters.ringbuf_drops;
runtime_stats.allowlist_exec_seen = counters.allowlist_exec_seen;

// 读取线程（HTTP handler）
const SchedDelayCollectorRuntimeStats sched_stats = sched_delay_->runtime_stats();
```

C++ 标准规定，非原子变量的并发读写是 **未定义行为**。虽然在 x86_64 上 64-bit 整数的错位读取通常不会 crash，但：
- 可能读到部分更新的不一致值
- 编译器可能做激进的变换（例如将多次读取合并）

**影响**:
- Prometheus scrape 时读到的 eBPF 统计可能不一致
- 跨架构移植时存在潜在 UB 风险

**建议**: 将 `SchedDelayCollectorRuntimeStats` 的字段改为 `std::atomic<uint64_t>`，或在读写时加锁。

---

### C4. JsonlEventSink::fsync_if_allowed 以只读 fd 调用 fdatasync

**文件**: `src/storage/jsonl_event_sink.cpp:403-408`

**问题描述**: POSIX 标准规定 `fdatasync()` 要求文件描述符**可写**，但此处用 `O_RDONLY` 打开：

```cpp
int fd = ::open(current_path_.c_str(), O_RDONLY | O_CLOEXEC);
if (fd >= 0) {
    ::fdatasync(fd);   // ← POSIX: undefined if not opened for writing
    ::close(fd);
}
```

**影响**:
- 技术上不符合 POSIX 规范（Linux 上实际能 work，因为内核刷的是 page cache 的 inode）
- 跨平台（非 Linux）或未来内核版本上可能无声地丢失同步
- 如果改用 `O_WRONLY` 则 `open()` 的权限检查会不同

**建议**: 改用 `std::ofstream` 关联的 fd。C++ 标准没有直接暴露 fd 的可移植方法，但在 Linux 上可以 `#ifdef __linux__` 下调用 `::fileno()`（POSIX 函数，GCC/MSVC 都支持）：

```cpp
#if defined(__linux__)
#include <cstdio>
int fd = ::fileno(stream_.rdbuf()->...);  // 或者直接 ::fileno(stream_)
// 或者改用 fcntl 获取 fd
::fdatasync(fd);
#endif
```

---

## 🟠 High

### H1. 调度延迟采集使用 double 解析大整数导致精度丢失

**文件**: `src/collectors/sched_delay_collector.cpp:47-54,267-278`

**问题描述**: `/proc/<pid>/sched` 文件中的 `se.statistics.wait_sum` 以**纳秒**为单位存储。对长时间运行（>3个月）的进程，这个累计值可达到 10^15 数量级。代码用 `double` 来解析，再乘以 1000 转微秒：

```cpp
double parsed = 0.0;
input >> parsed;              // double 只有 53-bit 有效数字
*wait_sum_us = static_cast<uint64_t>(parsed * 1000.0);
```

`double` 的 53-bit 十进制约 15-16 位有效数字。当 wait_sum_ns 超过 9×10^15（约 104 天）时，纳秒级精度丢失，导致计算出的增量不准确。

**影响**: 对长期运行的进程，调度延迟检测在高精度场景下的误差累积，可能导致漏告警或误告警。

**建议**: 使用 `std::from_chars` 直接解析为 `uint64_t`，避免 `double` 中转。

---

### H2. JSON 序列化未转义控制字符

**文件**: `src/storage/event_serializer.cpp:13-25`

**问题描述**: `append_json_string` 仅转义了 `"`, `\`, `\b`, `\f`, `\n`, `\r`, `\t`，但对于普通控制字符（U+0000 ~ U+001F）原样保留：

```cpp
default: output.push_back(c); break;  // 控制字符无声通过
```

根据 JSON 标准（RFC 8259 Section 7），所有控制字符必须用 `\uXXXX` 转义。

**影响**: 如果 `device_id`、`platform` 等配置字符串包含控制字符，输出的 JSONL 是非法 JSON。下游日志收集器（如 fluentd、Logstash）可能拒绝解析整行，导致事件丢失。

**建议**: 在 `default` 分支前增加控制字符检查：

```cpp
if (static_cast<unsigned char>(c) < 0x20) {
    char buf[8];
    std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
    output += buf;
} else {
    output.push_back(c);
}
```

---

### H3. NetworkEventSink append_wal 每条事件都重新打开/关闭文件

**文件**: `src/storage/network_event_sink.cpp:190-193`

**问题描述**: 每次追加 WAL 写入都重复 `open()` + 写入 + 析构 `close()` 的循环：

```cpp
std::ofstream stream(current_wal_path_, std::ios::out | std::ios::app | std::ios::binary);
stream << "0\t" << json_line;
// stream 离开作用域时析构 → close
```

**影响**: 在高事件率下（例如每秒数百个调度延迟事件），产生大量 `open()` 系统调用和 pages 的重复创建/销毁开销。在容器化环境中可能触发文件描述符泄漏（如果某次 open 后抛出异常）。

**建议**: 保持文件句柄打开，只在 segment 切换时重新打开：

```cpp
// 在初始化时打开
if (!wal_stream_.is_open()) {
    wal_stream_.open(current_wal_path_, std::ios::out | std::ios::app | std::ios::binary);
}
wal_stream_ << "0\t" << json_line;
```

---

### H4. ack_pending 每次确认后都完整重写 WAL

**文件**: `src/storage/network_event_sink.cpp:232-257,261-267`

**问题描述**: 网络恢复后，每次成功 flush 都会调用 `ack_pending()`，其中调用 `rewrite_wal_locked()`——删除所有 WAL segment 文件，然后逐条重写所有未确认事件：

```cpp
void NetworkEventSink::rewrite_wal_locked() {
    // 1. 删除所有 .wal 文件
    for (const fs::path& path : old_segments) { fs::remove(path, ec); }
    // 2. 逐个重写每条事件（每条都重新 open/close）
    for (WalRecord& record : pending_) {
        std::ofstream stream(current_wal_path_, ...);
        stream << "0\t" << record.json_line;
    }
}
```

**影响**: 网络断开数分钟后恢复时，如果积压了数千条事件，每次 flush 128 条（默认 batch_size）后都要完整重写剩余所有事件。性能呈 **O(n²)** 退化：随着 `pending_` 长度减少，每次重写的写入量线性降低，但重写次数与 batch_size 成反比。

极端情况：1 万条积压事件，batch_size=128 → 需要 78 次 flush，每次 flush 触发一次完整的 WAL 重写，总写出量 ≈ 10000 + 9872 + 9744 + ... ≈ 390k 条次的写入，约 20MB 的 IO。

**建议**:
1. WAL 采用 segment 粒度删除：ack 后只删除空的 segment 文件，而非重写整个 WAL
2. 或：ack 本质上就是把 `pending_` 中前 N 条移掉，WAL 文件保留末尾内容，下次启动时只加载最后未 ack 的部分

---

### H5. IoDelayCollector 当磁盘 stats 被重置时的边界情况

**文件**: `src/collectors/io_delay_collector.cpp:50-53`

**问题描述**: 当内核重置磁盘统计（如设备重新挂载、驱动卸载重装）时，当前代码检查：

```cpp
if (stats.read_ios < previous.read_ios || stats.write_ios < previous.write_ios ||
    stats.read_time_ms < previous.read_time_ms || stats.write_time_ms < previous.write_time_ms ||
    stats.io_time_ms < previous.io_time_ms) {
    continue;  // 跳过一个采样周期
}
```

问题在于，如果重置发生在这段间隔的中间（两次 `collect()` 调用之间），`delta_io_time` 会大于实际的 `elapsed_ms`，导致 `util_percent` 计算为 >100%。代码用 `std::min(100.0, ...)` 做了截断，但 `avg_await_ms` 的计算结果是基于 delta 的没有截断，可能异常高。

**影响**: 设备重置后的第一个采样周期数据不可用，但被截断而非跳过；`avg_await_ms` 可能出现极高异常值。

**建议**: 当 `stats.io_time_ms < previous.io_time_ms` 时（已检查），进一步检查 `delta_io_time > elapsed_ms`（超出物理可能）时跳过本次采样。

---

## 🟡 Medium

### M1. 配置文件不存在或格式错误时静默使用默认值

**文件**: `src/core/config.cpp:34,157-160`

**问题描述**: 配置文件路径错误或不存在时，`parse_ini()` 返回空的 INI 数据，`load_or_default()` 静默使用所有默认值：

```cpp
MonitorConfig ConfigLoader::load_or_default(const std::string& path)
{
    MonitorConfig config;
    const Ini ini = parse_ini(path);  // 文件打不开？空 Ini
    // 所有 assign_* 都找不到值 → 全部默认值
    // 无任何日志或警告
    return config;
}
```

**影响**: 用户输入了错误的配置文件路径，守护进程以错误配置运行且无任何告警。

**建议**: 检查 `std::ifstream` 是否成功打开，失败时 `stderr` 输出警告。

### M2. 配置项拼写错误静默忽略

**文件**: `src/core/config.cpp:38-56`

**问题描述**: INI 解析器对无法识别的 section/key 完全静默，用户写错配置键名不会得到任何反馈：

```ini
# 用户意图：设置 self_protection.enable = false
# 实际写错：
self_protection.enable = false      # 解析器忽略（不存在此键）

# 或者值写错：
enable = ture                        # parse_bool("ture") → false，无警告
```

**影响**: 配置项无声地被忽略，用户以为某个功能已关闭但实际仍开着。**尤其危险**：用户将 `enable` 写为 `disabled`，`parse_bool` 返回 `false`，貌似"正确"但原因误判。

**建议**: 在 `validate()` 或 `load_or_default()` 末尾 dump 配置摘要到 stderr，方便用户核对生效的配置值。

### M3. INI 解析器不支持 Windows 换行符

**文件**: `src/core/config.cpp:38`

**问题描述**: `std::getline(stream, line)` 默认会去掉换行符，但在 Windows 的 `\r\n` 场景下，`trim()` 虽然去掉了末尾空格，但 `section` 名在 `[section]` 解析时若含有 `\r`，会变成 `"section\r"` 导致 section 匹配失败。

**影响**: 在 Windows 上编辑的配置文件直接传到 Linux 使用时，section 匹配失败，配置完全失效。

**建议**: 在 `trim()` 中增加 `\r` 的移除。

### M4. 最大扫描进程数无上限

**文件**: `src/collectors/sched_delay_collector.cpp:84`

**问题描述**: 遍历 `/proc` 目录项时，对进程数量没有 `max_scan_processes` 的上限控制。在大量容器（数千 PID）的节点上，`directory_iterator` 加上 `cached_comm` 的读取操作可能导致采集耗时数秒。

当 `sched_process_whitelist` 非空时，**所有** PID 都进入循环体，读取 `comm` 文件，然后才被 `should_scan_process` 过滤。

**影响**: 在大型系统上采集循环耗时超过 `sched_collector_overrun_warning_ms`，持续触发 MonitorOverrun 事件。

**建议**: 增加可配置的 `sched_proc_max_scan_processes` 参数，防止在超大型机器上采集超时。

### M5. LinuxProcReader 缓冲区溢出保护仅是单个 null 终止符

**文件**: `src/collectors/linux_proc_reader.cpp:35-36`

**问题描述**: 当 `/proc/meminfo` 内容超过 8191 字节时（当前 Linux 内核中这不可能，但不能假设未来不会）：

```cpp
const ssize_t n = ::read(fd_, buffer_.data(), buffer_.size() - 1);
```

`buffer_` 大小是 8192，读取最多 8191 字节，保证 null 终止。但如果 `/proc` 文件变得更大（内核未来版本），数据会被截断且无告警。

**影响**: 当前无影响（`/proc/meminfo` < 8KB），但这是一个脆弱性预留。

**建议**: 添加 `n == buffer_.size() - 1` 的截断检测日志。

### M6. MetricsServer 使用短连接但 Connection: close 意味着每次 scrape 重新连接

**文件**: `src/runtime/metrics_server.cpp:138-163`

**问题描述**: HTTP 响应头始终是 `Connection: close`，意味着每个 HTTP 请求都会在 `accept()` 后新建连接、处理、关闭。虽然这对 Prometheus scrape 模式来说是可接受的，但在高 scrape 频率（如 < 5s）下，`poll()` + `accept()` + `recv()` + `send()` + `close()` 的循环会成为瓶颈。

**影响**: 只有在高频率 scrape（< 1s）时才会成问题。默认情况下 Prometheus 的 scrape_interval 是 15s。

**建议**: 增加一个 `Connection: keep-alive` 模式（可选），支持多请求。

### M7. NetworkEventSink 仅支持 IPv4

**文件**: `src/storage/network_event_sink.cpp:296`

```cpp
hints.ai_family = AF_INET;  // 仅 IPv4
```

**影响**: 配置 IPv6 endpoint 或 host 解析为 IPv6 地址时连接会失败，无明确错误信息。

**建议**: 改为 `AF_UNSPEC`，按 `getaddrinfo` 返回的顺序尝试。

### M8. fast_collect_interval_ms 与 sched 采集共享同一 interval

**文件**: `src/runtime/monitor.cpp:188`

```cpp
const auto interval = std::chrono::milliseconds(config_.fast_collect_interval_ms);
```

sched_collect_loop 硬编码使用 `fast_collect_interval_ms`，而不是独立可配置的调度采集间隔。

**影响**: 用户无法分别控制内存采集和调度/IO 采集的频率。如果希望内存 2s、调度 5s，无法配置。

---

## 🔵 Low

### L1. `should_consider_process_id` 命名与实际行为不符

**文件**: `src/collectors/sched_delay_collector.cpp:189-199`

```cpp
bool SchedDelayCollector::should_consider_process_id(int32_t pid) const
{
    if (!config_.sched_process_whitelist.empty()) {
        return true;   // 放行所有，实际过滤在 should_scan_process
    }
    return pid == ::getpid();  // 无白名单则只监控自己
}
```

函数名暗示它"判断某个 PID 是否应被考虑"，但白名单非空时它实际放行所有 PID。真正的过滤在 `should_scan_process` 中按进程名匹配。

**建议**: 重命名为 `should_filter_by_process_whitelist()` 或添加注释说明行为。

### L2. 部分字符串比较使用 `rfind` 而非 `starts_with`

**文件**: `src/runtime/metrics_server.cpp:167,170`、`src/storage/network_event_sink.cpp:355`

```cpp
if (request.rfind("GET /metrics ", 0) == 0)  // 正确，但可读性差
```

这里的 `rfind(...) == 0` 逻辑等价于 C++20 的 `starts_with()`，但在不支持 C++20 的环境中可用 `std::string_view::substr(0, len) == prefix` 替代，可读性更好。

**建议**: 抽取一个 `starts_with` 工具函数（项目已经在 `io_delay_collector.cpp` 中有这个函数了，可复用）。

### L3. CollectorFailure 事件的 evidence "collector_id" 类型模糊

**文件**: `src/runtime/monitor.cpp:277`

```cpp
set_evidence_key(event.evidence[0], "collector_id");
event.evidence[0].value = static_cast<double>(collector_id);  // 1,2,3,4 的 double 值
```

collector_id 1~4 是魔术数字，分布在 `monitor.cpp:23-26` 中定义：

```cpp
constexpr uint32_t kMeminfoCollectorId = 1;
constexpr uint32_t kSelfStatusCollectorId = 2;
constexpr uint32_t kSchedDelayCollectorId = 3;
constexpr uint32_t kIoDelayCollectorId = 4;
```

**影响**: 告警查看者需要查阅源码才知道 `collector_id=3` 对应的是 SchedDelay 采集器。

**建议**: 将 collector_id 改为可读字符串形式，或至少在所有文档/总结日志中包含映射关系。

### L4. 测试框架过于原始

**文件**: `tests/unit/*.cpp`

所有测试使用自制的 `CHECK()` 宏：

```cpp
#define CHECK(condition)
    do {
        if (!(condition)) {
            std::cerr << "check failed: " #condition << " at line " << __LINE__ << "\n";
            return EXIT_FAILURE;
        }
    } while (false)
```

- 没有测试夹具（setup/teardown）
- 没有断言变体（CHECK_EQ, CHECK_NE, CHECK_NEAR）
- 没有测试自动发现机制，需要手动维护 CMakeLists.txt
- 第一个 CHECK 失败后测试立即退出，不报告后续失败

**建议**: 迁移到 Google Test 或 Catch2，或至少扩展 CHECK 宏系列。

---

## 统计汇总

| 级别 | ID | 数量 |
|------|----|------|
| 🔴 Critical | C1 ~ C4 | 4 |
| 🟠 High | H1 ~ H5 | 5 |
| 🟡 Medium | M1 ~ M8 | 8 |
| 🔵 Low | L1 ~ L4 | 4 |
| **合计** | | **21** |

## 按模块分布

| 模块 | Critical | High | Medium | Low |
|------|----------|------|--------|-----|
| 配置系统 (config) | 0 | 0 | 3 | 0 |
| 采集器 (collectors) | 1 | 3 | 2 | 1 |
| 规则引擎 (rules) | 0 | 0 | 0 | 0 |
| 事件调度 (dispatcher) | 0 | 0 | 0 | 0 |
| 存储序列化 (serializer) | 0 | 1 | 0 | 0 |
| JSONL Sink | 1 | 0 | 0 | 0 |
| Network Sink | 0 | 2 | 1 | 0 |
| Metrics | 2 | 0 | 1 | 0 |
| eBPF 模块 | 1 | 0 | 0 | 0 |
| 测试 | 0 | 0 | 0 | 1 |

---

## 最值得优先修复的前 3 项

1. **C1 — MetricRegistry 无限增长**: 长时间运行存在 OOM 风险，是最严重的运行时问题
2. **C2 — 扫描上限误报失败**: 产生持续告警噪音，影响系统可信度
3. **C3 — RuntimeStats 数据竞争**: 标准未定义行为，虽然当前架构多数情况下不会 crash 但需要修复
