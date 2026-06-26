# 2026-06-24 工程化目录重构与一键脚本

## 已完成

- 将项目形态调整为守护进程入口、核心库、独立 eBPF 模块、测试、工具和脚本分层。
- 将对外头文件根目录调整为 `include/minilisysm/`，但 C++ namespace 暂时保持 `lisysm`。
- 将运行入口移动到 `apps/minilisysm-agent/`。
- 将默认配置移动到 `configs/`。
- 将单元测试移动到 `tests/unit/`。
- 将 benchmark 工具移动到 `tools/bench/`。
- 将 eBPF 程序和用户态 collector 移动到独立 `ebpf/` 模块。
- 新增 `cmake/` 模块集中管理构建选项和编译器告警。
- 新增 `scripts/install_deps.sh`、`scripts/build.sh`、`scripts/verify.sh`，面向 Ubuntu/WSL 做依赖安装、构建和验证。

## 设计说明

- 本次只做工程化结构和构建入口重构，不改变核心业务逻辑。
- eBPF 仍是可选增强，默认关闭，不要求普通构建安装 libbpf 或 bpftool。
- 依赖安装脚本只正式支持 Ubuntu/WSL，目标嵌入式 Linux 后续单独适配。
