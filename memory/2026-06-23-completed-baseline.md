# 2026-06-23 Completed Baseline

Workspace: `E:\minilisysm`

## 已完成实现

- 创建 C++17 CMake 工程，用于 Lisysm Linux 稳定性监控链路初版实现。
- 建立目录结构：`include/lisysm/`、`src/`、`config/`、`tests/`、`tools/`、`docs/`。
- 实现 INI 风格配置加载与校验。
- 实现固定大小内部事件模型，包含事件等级、状态、类型、证据和三类时间戳。
- 实现有界 SPSC ring buffer，包含丢弃计数和 high-watermark 统计。
- 实现 Linux `/proc/meminfo` 采集器，以及可复用读取器和预分配缓冲区。
- 实现内存压力规则状态机，支持 Warning、Critical、Recovery 生命周期。
- 修复规则引擎：Critical 连续命中独立计数，不再复用 Warning 命中次数。
- 实现后台 JSONL 事件持久化，包含文件滚动、缓存容量上限和 Critical fsync 限频。
- 实现 Linux 线程策略辅助函数：CPU affinity 和 nice，非 Linux 下安全降级。
- 增加主程序 `minilisysm`、单元测试和 SPSC benchmark。
- 增加 `README.md`、`docs/ARCHITECTURE.md`、默认配置和 `.gitignore`。

## WSL 验证结果

- WSL 中自动路径翻译无法直接识别 `E:\minilisysm`，已手动挂载 `E:` 到 `/mnt/e`。
- 安装构建依赖：`build-essential`、`cmake`、`ninja-build`。
- Release 配置和构建通过：CMake + Ninja + GCC 13.3.0。
- Release 单测通过：`spsc_queue`、`rule_engine`。
- SPSC benchmark 跑通：1,000,000 次 push/pop，约 14 ns 每轮。
- ASan/UBSan Debug 构建通过。
- ASan/UBSan 单测通过。
- 运行态烟测通过：强制提高内存阈值后，成功生成 `monitor_started` 和 `memory_pressure/critical` JSONL 事件。

## 验证中修复的问题

- Release 测试原先使用 `assert`，在 `-DNDEBUG` 下会被关闭；已改为显式 `CHECK`。
- 规则引擎原先会让 Warning 命中次数直接影响 Critical 触发；已增加 `critical_hit_count` 独立计数。

## 剩余风险

- 尚未进行长时间稳定性运行。
- 尚未在真实车载或嵌入式 Linux 目标机验证时延和资源占用。
- CPU affinity、nice、fsync 和 collector 开销仍需目标平台实测。
- 还未实现调度延迟采集、I/O 阻塞规则、中断风暴规则、自身 RSS 保护、eBPF 增强采集、异步上传和断网补传闭环。

## 后续建议

1. 增加线程调度延迟 collector，优先支持 `/proc/<pid>/task/<tid>/sched`。
2. 增加自身 RSS 和队列拥塞保护事件。
3. 增加持久化线程阻塞检测。
4. 引入异步上传模块，但保持快速路径不直接访问网络。
5. 在目标 Linux 平台做 200 Hz 控制周期干扰测试。
