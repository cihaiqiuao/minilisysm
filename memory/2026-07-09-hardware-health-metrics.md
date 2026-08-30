# 2026-07-09 硬件健康 metrics 和状态页展示

## 变更

- 新增 `HardwareHealthCollector`，低频读取 Linux 通用硬件健康节点。
- 电池指标来自 `/sys/class/power_supply`，包括当前电量、健康度、循环次数和温度。
- 存储寿命指标来自 `/sys/block/*/device/life_time` 和 `pre_eol_info`，按 eMMC lifetime bucket 估算已使用寿命百分比。
- 内存健康指标来自 EDAC `ce_count` / `ue_count`，用于展示 ECC corrected / uncorrected 错误。
- `Monitor` 在低频采集线程中调用硬件健康 collector；`MonitorMetrics` 将样本导出为 Prometheus metrics。
- `/status` 页面新增硬件健康表，并在顶部卡片展示电池健康、存储寿命已用和 ECC 未纠错错误。

## 新增 metrics

- `minilisysm_battery_capacity_percent`
- `minilisysm_battery_health_percent`
- `minilisysm_battery_cycle_count`
- `minilisysm_battery_temperature_celsius`
- `minilisysm_storage_lifetime_used_percent`
- `minilisysm_storage_pre_eol_info`
- `minilisysm_memory_ecc_corrected_total`
- `minilisysm_memory_ecc_uncorrected_total`

## 边界

- 当前只接通用 Linux sysfs/EDAC，不接 CAN、BMS、厂商 SDK 或私有 UFS health 接口。
- 目标节点不存在时不产生告警，页面显示暂无，避免在无电池或无 EDAC 的平台上制造噪声。

## 验证

- 新增 `test_hardware_health_collector`，用临时目录模拟 battery、eMMC lifetime 和 EDAC 计数。
- 已通过 `ctest --test-dir build/status-demo --output-on-failure -R hardware_health_collector`。
- 已通过 `ctest --test-dir build/status-demo --output-on-failure -R metrics_server`。
