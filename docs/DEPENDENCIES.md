# 依赖管理说明

## 当前依赖

`minilisysm` 当前不 vendoring 第三方源码，也没有 `vcpkg.json` 或 `conanfile`。

基础构建依赖由 `scripts/install_deps.sh` 安装：

- `build-essential`
- `cmake`
- `ninja-build`
- `pkg-config`

eBPF 是可选能力，只有执行 `scripts/install_deps.sh --with-ebpf` 和 `scripts/build.sh --ebpf` 时才需要：

- `clang`
- `llvm`
- `bpftool`
- `libbpf-dev`
- `linux-headers-$(uname -r)`，不可用时脚本只提示，不阻断基础构建

## 第三方库引入规则

后续如果引入 `fmt`、`spdlog`、`nlohmann/json` 等第三方库，必须统一管理：

- 能用 vcpkg 或 Conan 时，优先通过清晰的 manifest 管理版本。
- 目标环境不适合包管理时，才允许 vendoring 到 `third_party/<name>/`。
- vendored 依赖必须记录版本、来源、许可证和更新方式。
- 不允许把第三方源码散落复制到 `src/`、`include/`、`tools/` 或测试目录。

当前不新增空的 vcpkg/Conan 配置，避免项目还没有外部库时就引入无意义的维护成本。
