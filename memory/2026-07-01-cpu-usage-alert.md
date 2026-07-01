# 2026-07-01 CPU 占用率报警接入

## 本次完成

- 新增 `CpuUsageCollector`，默认读取 `/proc/stat` 第一行 `cpu`。
- collector 在第一次采样时建立基线，后续通过总 jiffies 与 idle jiffies 差分计算整体 CPU 使用率。
- 新增 `[cpu_usage_rule]` 配置段：
  - `enable`
  - `mode`：`total`、`per_core` 或 `both`
  - `core_whitelist`
  - `warning_percent`
  - `critical_percent`
  - `recovery_percent`
  - `continuous_warning_windows`
  - `continuous_critical_windows`
  - `recovery_windows`
- 新增事件类型 `cpu_usage_risk`，中文 summary 显示为 `CPU 占用风险`。
- `RuleEngine` 接入 CPU 阈值状态机，支持 warning、critical、recovery 生命周期。
- `Monitor` fast collector loop 接入 CPU 采集、规则评估、collector failure 限频事件。
- `/metrics` 新增：
  - `minilisysm_cpu_usage_percent{cpu="total|cpuN"}`
  - `minilisysm_cpu_delta_total_jiffies{cpu="total|cpuN"}`
  - `minilisysm_cpu_delta_idle_jiffies{cpu="total|cpuN"}`
  - `minilisysm_collector_failures_total{collector="cpu_usage"}`
- 更新 README、ARCHITECTURE 和 DEPLOYMENT 文档。

## 设计边界

- 默认只监控整体 CPU 使用率；可通过 `mode=per_core` 或 `mode=both` 打开分核心监控。
- 分核心事件的 target 为 `cpu0`、`cpu1` 等，整体 CPU 事件 target 为 `total`。
- 当前使用 `/proc/stat` 低依赖路线，没有引入 eBPF 或 perf。
- `/proc/stat` 的 `guest` 和 `guest_nice` 字段只解析保存，不计入 total jiffies，避免在虚拟化场景中重复计算。

## 验证

已通过：

```bash
bash scripts/verify.sh
cmake --build build/release-vcpkg --target test_cpu_usage_collector test_rule_engine minilisysm
ctest --test-dir build/release-vcpkg --output-on-failure -R 'cpu_usage_collector|rule_engine'
git diff --name-only -- '*.cpp' '*.hpp' | xargs clang-format --dry-run --Werror
git diff --check
```

结果：

- Release/vcpkg 构建通过。
- 15/15 CTest 全部通过。
- 新增 `cpu_usage_collector` 单测通过。
- CPU rule 生命周期单测通过。

## 后续可选

- 区分 user/system/iowait/steal 占比，避免只看整体 CPU 时漏掉具体压力来源。

## 后续补充：qalog 日志查看命令

用户希望输入自定义命令 `qalog` 直接打印日志。

本次补充：

- 新增 `scripts/qalog`。
- CMake 安装时把脚本安装到 `install/bin/qalog`。
- 默认打印最新 `logs/events/summary/*.summary.log`。
- 支持：
  - `-f` / `--follow`
  - `-n` / `--lines`
  - `--summary`
  - `--jsonl`
  - `--agent`
  - `--root PATH`
- 更新 README 和 DEPLOYMENT 文档。

后续又补充了默认状态面板：

- `qalog` 默认从 `http://127.0.0.1:9108/metrics` 打印当前状态，而不是默认打印事件 summary。
- 状态面板显示 up、事件总数、CPU、内存、队列、collector、I/O 和 sink 状态。
- `qalog -f` 在 status 模式下每 2 秒刷新一次。
- metrics 不可用时回退到本机 `/proc` 状态，仍能显示 CPU、内存、load、root 磁盘占用、minilisysm 进程状态和最新 summary 路径。
- 原事件查看能力保留为 `qalog --summary`、`qalog --jsonl`、`qalog --agent`。

后续又补充了彩色终端渲染：

- 自动检测 stdout 是否为终端，终端输出时启用 ANSI 颜色，管道/重定向时自动纯文本。
- 新增 `--color` 和 `--no-color`。
- CPU、内存、队列加状态色和 ASCII usage bar。
- collector failure、queue drop、sink errors、WAL pending 等异常计数用红色突出。
