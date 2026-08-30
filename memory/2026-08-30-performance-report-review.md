# 2026-08-30 性能实测报告复核

## 范围

- 复核报告：`E:\学习文件\音视频\项目2-26\minilisysm-performance-report-20260830T095350Z`。
- 审计编号：`20260830T095350Z`，状态 `partial`，源码为 dirty tree（HEAD `af484c6eae8a483d3c3adc120f58954ac66d38c0`）。
- 本次仅做只读证据与源码映射检查，没有修改实现或重新采集。

## 可靠结论

- 报告包完整：44 个 raw 文件与 manifest 的大小/SHA-256 一致，`latest.md` 与 `report.md` 字节一致，validator 退出码 0。
- persistence 启用时，SIGTERM 后 `Monitor` 二次 `stop()` 命中已 shutdown 的 spdlog，Release 退出 139；sanitizer 栈定位到 `EventDispatcherGroup::stop()`。
- CTest 为 13/16；CPU、I/O baseline 与 RuleEngine 在确认本轮目标前执行 TTL prune，造成活跃目标采样空洞或重复告警。
- 4.586 s / 9.525 s 停止延迟与低频线程 10 s 不可通知 `sleep_until`、stop 中 join 顺序一致。

## 报告口径问题

- `strace` 命令在 12 s 时发送 SIGTERM，但 trace 实际约 22–23 s 后结束；`18401 / 12 = 1533.4 wakeups/s` 无效。固定 2 ms 轮询仍成立，wakeup/s 需要记录真实起止时间后重测。
- SPSC benchmark 是同一线程内 push 后立即 pop，不覆盖跨线程/跨核 cache coherence，不能据此排除生产 SPSC 路径。
- forced Critical 只有一个受 strace、首次建文件影响的样本；4.209 ms `fdatasync` 是该样本事实，不是延迟分布。
- 根目录 `latest.md` 中 33 个 `raw/...` 相对链接失效；运行目录的 `report.md` 链接正常。
- dirty snapshot 的逐文件哈希清单/补丁未保存在报告包；三项失败测试的 repeat 原始输出也未单独留存。

## 报告遗漏的静态风险

- sched/io 事件 `window_sec` 误用 `fast_collect_interval_ms`；默认会把约 30 s 的 3 个低频窗口写成 3 s。所有 Critical 事件还统一使用 warning window 数量。
- Network WAL ack 先删除旧 segment，再直接重写 pending，没有 temp + fsync + atomic rename；崩溃/掉电窗口可能丢未确认 backlog，风险高于报告只提到的 O(N²)。
- JSONL 默认每分钟最多 6 次 Critical `fdatasync`，第 7 条起不保证立即 durable；失败尝试仍计入 `fsync_count`。首次创建文件也未同步父目录，严格掉电语义未建立。
- metrics/log disabled baseline 仍更新 MetricRegistry，并创建 null logger/thread pool；1.192% 是整进程空闲成本，不能唯一归因 dispatcher。

## 建议顺序

1. P0：修复 stop/logger 生命周期并补 persistence-enabled SIGTERM 重复测试。
2. P0：修复 TTL 当前目标语义，恢复全量测试。
3. P1：修复 sched/io/critical `window_sec` 并补字段断言。
4. P1：明确 JSONL/Network WAL durability SLA，再做原子 WAL 与 sync 语义测试。
5. P1：记录真实采集起止时间，在目标板以真实事件率复测 CPU、wakeup、停止 P99 与端到端分位数。
