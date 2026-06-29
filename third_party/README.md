# 第三方依赖管理规则

`minilisysm` 当前不内置第三方源码。

当前依赖按系统依赖、工具链依赖或 vcpkg manifest 依赖处理：

- C++17 编译器、CMake、Ninja 和 pkg-config 由 `scripts/install_deps.sh` 安装。
- 可选 eBPF 能力使用系统包，例如 clang、llvm、bpftool 和 libbpf。
- C++ 第三方库使用根目录 `vcpkg.json` 管理，当前包含 `spdlog`。

后续如果引入 `fmt`、`nlohmann/json`、`GoogleTest` 等第三方库，统一按下面规则管理：

- 目标构建环境支持时，优先加入根目录 `vcpkg.json`。
- 目标平台不适合包管理时，才允许把源码放到 `third_party/<name>/`。
- 内置源码必须记录版本、上游地址、许可证和更新方式。
- 不允许把复制来的第三方源码散落到 `src/`、`include/`、`tools/` 或测试目录。

只有在 vcpkg 不适合目标平台或目标依赖无法通过 vcpkg 管理时，才允许在本目录增加 vendored 依赖。
