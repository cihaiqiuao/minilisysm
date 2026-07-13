# 2026-07-13 RK3588S train_stability 监控接入

## 已接入配置

- systemd 服务：`minilisysm.service`，以 `cat` 用户运行。
- 被监控服务主线程进程名：`train_stability`。
- 配置文件：`install/etc/minilisysm/lisysm_monitor.ini`。
- 调度延迟白名单：

```ini
[sched_delay_rule]
process_whitelist=train_stability
```

- Prometheus 指标端点：`http://127.0.0.1:9108/metrics`。

## RK3588S 内核兼容性发现

本板内核的 `/proc/<pid>/task/<tid>/sched` 包含
`nr_involuntary_switches`，但没有 `se.statistics.wait_sum`；
`schedstat` 也为空。因此当前 `/proc` 调度延迟采集器不能生成
`minilisysm_sched_*` 延迟样本，尽管进程白名单匹配和采集线程本身正常。

不要把 `minilisysm_collector_failures_total{collector="sched_delay"}=0`
理解为已经获得了调度等待时间；它只表示采集器没有发生 I/O/扫描错误。

## 后续选择

要获得真实的单线程调度等待延迟，需要使用 eBPF `sched_switch` 数据源。
当前板端缺少 `/sys/kernel/btf/vmlinux`，无法构建项目现有的 CO-RE eBPF
collector；同时运行 eBPF loader 需要 root/CAP_BPF。恢复 BTF/内核支持后，
以 `MINILISYSM_ENABLE_EBPF=ON` 构建并将 `source=ebpf` 即可。

在此之前，minilisysm 仍持续提供系统级 CPU、内存、I/O、监控器自身健康和
systemd 自动重启监控；不能将其 `/proc` 调度规则当作 train_stability 的实际
延迟告警来源。

## 白名单进程资源可观测性

为避免白名单只影响内部过滤、`qalog` 无法确认目标的问题，监控器在快速采集
周期额外导出匹配进程的指标：

- `minilisysm_whitelisted_process_up{process}`
- `minilisysm_whitelisted_process_cpu_usage_percent{process,pid}`
- `minilisysm_whitelisted_process_rss_bytes{process,pid}`
- `minilisysm_whitelisted_process_threads{process,pid}`

`qalog` 的 `Whitelisted processes` 区块显示上述数据。CPU 为该进程在一个
采样周期内占用的单核百分比，因此多线程进程可以超过 100%。
