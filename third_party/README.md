# 第三方依赖管理规则

`minilisysm` 当前不内置第三方源码。

当前依赖按系统依赖或工具链依赖处理：

- C++17 编译器、CMake、Ninja 和 pkg-config 由 `scripts/install_deps.sh` 安装。
- 可选 eBPF 能力使用系统包，例如 clang、llvm、bpftool 和 libbpf。

后续如果引入 `fmt`、`spdlog`、`nlohmann/json` 等第三方库，统一按下面规则管理：

- 目标构建环境支持时，优先使用 vcpkg 或 Conan 这类包管理 manifest 固定版本。
- 目标平台不适合包管理时，才允许把源码放到 `third_party/<name>/`。
- 内置源码必须记录版本、上游地址、许可证和更新方式。
- 不允许把复制来的第三方源码散落到 `src/`、`include/`、`tools/` 或测试目录。

在真正引入第一个非系统第三方库之前，不添加空的 vcpkg 或 Conan 配置文件，避免制造没有实际价值的维护成本。
