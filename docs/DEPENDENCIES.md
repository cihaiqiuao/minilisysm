# 依赖管理说明

## 当前依赖

`minilisysm` 当前不 vendoring 第三方源码。C++ 第三方库由根目录 `vcpkg.json` 统一管理。

系统工具链依赖由 `scripts/install_deps.sh` 安装：

- `build-essential`
- `clang-format`
- `clang-tidy`
- `cmake`
- `curl`
- `git`
- `lcov`
- `ninja-build`
- `pkg-config`
- `tar`
- `unzip`
- `zip`

C++ 第三方库：

- `spdlog`：Agent 自身运行日志框架，支持分级、异步和文件滚动。

vcpkg 默认由构建脚本自动引导：

```bash
./scripts/bootstrap_vcpkg.sh
./scripts/build.sh
./scripts/verify.sh
```

如果目标环境已经通过系统包提供 C++ 依赖，可以使用：

```bash
./scripts/build.sh --no-vcpkg
./scripts/verify.sh --no-vcpkg
```

eBPF 是可选能力，只有执行 `scripts/install_deps.sh --with-ebpf` 和 `scripts/build.sh --ebpf` 时才需要：

- `clang`
- `llvm`
- `bpftool`
- `libbpf-dev`
- `linux-headers-$(uname -r)`，不可用时脚本只提示，不阻断基础构建

## 第三方库引入规则

后续如果引入 `fmt`、`nlohmann/json`、`GoogleTest` 等 C++ 第三方库，必须统一管理：

- 优先通过 `vcpkg.json` manifest 管理版本。
- 新增 CMake 查找逻辑应集中放在 `cmake/MinilisysmDependencies.cmake`。
- 目标环境不适合包管理时，才允许 vendoring 到 `third_party/<name>/`。
- vendored 依赖必须记录版本、来源、许可证和更新方式。
- 不允许把第三方源码散落复制到 `src/`、`include/`、`tools/` 或测试目录。
