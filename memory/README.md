# 项目记忆

这个目录用于保存 `minilisysm` 项目内的长期上下文，方便后续继续开发时快速恢复状态。

## 当前索引

- [2026-06-23-completed-baseline.md](2026-06-23-completed-baseline.md)：已完成的工程内容、验证结果、修复点和后续风险。
- [2026-06-23-self-protection-sched-delay.md](2026-06-23-self-protection-sched-delay.md)：自身保护规则和调度延迟规则的实现与验证记录。
- [2026-06-25-queue-protection-and-spsc-move.md](2026-06-25-queue-protection-and-spsc-move.md)：队列满载保护、Critical 保留槽位、source/sink 队列压力和 SPSC move 语义。
- [2026-06-25-ebpf-low-overhead-collector-optimization.md](2026-06-25-ebpf-low-overhead-collector-optimization.md)：eBPF 低开销过滤、allowlist map、ringbuf callback 优化、proc fallback 缓存和 deadline 采集循环。
- [2026-06-25-production-hardening.md](2026-06-25-production-hardening.md)：Metrics、NetworkSink/WAL、eBPF 生命周期与聚合、RuleEngine 模板化的生产化增强记录。
- [2026-06-25-production-hardening-pitfalls.md](2026-06-25-production-hardening-pitfalls.md)：生产化增强过程中的编译、接口、eBPF、WAL 和 WSL/Git 踩坑记录。
- [2026-06-25-factory-method-refactor-cn.md](2026-06-25-factory-method-refactor-cn.md)：工厂方法重构的中文说明、设计原因和边界。
- [2026-06-25-interface-boundary-and-dependency-policy.md](2026-06-25-interface-boundary-and-dependency-policy.md)：接口边界抽取、`interfaces/` 目录职责和第三方依赖管理预留。
- [2026-06-29-build-runtime-quality-gates.md](2026-06-29-build-runtime-quality-gates.md)：构建/install 布局、运行脚本、日志输出、事件 summary、systemd 服务、基础质量门禁和 vcpkg 依赖管理记录。
- [2026-07-01-cpu-usage-alert.md](2026-07-01-cpu-usage-alert.md)：CPU 占用率 collector、报警规则、metrics、summary 和验证记录。
- [2026-07-03-stability-fixes.md](2026-07-03-stability-fixes.md)：低频采集 interval、动态 metrics stale label 清理和 eBPF runtime stats 同步修复记录。
- [2026-07-03-monitor-metrics-decoupling.md](2026-07-03-monitor-metrics-decoupling.md)：将 metrics 记录和渲染适配从 `Monitor` 抽到 `MonitorMetrics` 的解耦记录。
- [2026-07-09-browser-status-page.md](2026-07-09-browser-status-page.md)：浏览器和手机状态页，复用 metrics HTTP 服务暴露 `/status`。
- [2026-07-09-hardware-health-metrics.md](2026-07-09-hardware-health-metrics.md)：电池、存储寿命和 EDAC 内存健康 metrics 及状态页展示。
- [2026-07-13-rk3588s-train-stability-monitoring.md](2026-07-13-rk3588s-train-stability-monitoring.md)：RK3588S 白名单进程资源指标、RSS 增长告警和 `/proc` 调度数据边界。
- [2026-08-02-runtime-reliability.md](2026-08-02-runtime-reliability.md): cooldown, dynamic-state TTL, and Metrics IPv4 allowlist hardening.
- [2026-08-30-performance-report-review.md](2026-08-30-performance-report-review.md)：实测性能报告的证据复核、口径问题、遗漏风险和修复优先级。
- [2026-08-30-performance-report-fixes.md](2026-08-30-performance-report-fixes.md)：SIGTERM/停止延迟、TTL、窗口口径、WAL/JSONL 耐久性及 CI 质量门禁修复与最终验证。
## 约束

- 只记录项目上下文、设计决策、验证结果和后续计划。
- 不记录密钥、密码、Token、私钥或任何凭据。
- 细节放在独立 Markdown 文件中，主索引保持简洁。
