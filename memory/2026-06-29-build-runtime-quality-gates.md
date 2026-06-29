# 2026-06-29 构建运行链路与基础质量门禁

## 背景

本轮从用户实际编译、运行、查看事件、开机自启动和工程质量审查几个角度，对 `minilisysm` 做了一轮工程化补强。目标不是重写核心监控逻辑，而是让项目更接近可部署、可验证、可交接的守护进程工程。

主要关注点：

- `build/` 和最终产物目录职责混乱。
- 程序运行时需要手动传配置，不符合默认安装体验。
- 缺少统一运行脚本、默认绑核和服务化入口。
- 事件 JSONL 适合机器处理，但人工查看不够直观。
- Agent 自身启动过程缺少明确运行日志。
- 缺少 CI、格式化、静态检查、Sanitizer 和覆盖率入口。

## 构建与安装布局

已将构建和安装语义收口：

- `build/` 只作为 CMake/Ninja 中间产物目录。
- 默认 Release 构建目录为 `build/release/`。
- 最终可运行产物安装到 `install/`。
- ASan 和 eBPF 变体分别安装到 `install-asan/` 和 `install-ebpf/`。
- 安装后的主程序位于 `install/bin/minilisysm`。
- 默认配置安装到 `install/etc/minilisysm/lisysm_monitor.ini`。
- 文档安装到 `install/share/doc/minilisysm/`。

程序默认配置路径也已工程化：

- `install/bin/minilisysm` 默认读取安装目录下的 `etc/minilisysm/lisysm_monitor.ini`。
- 用户仍可通过命令行第一个参数临时指定其他配置。
- 这样普通运行不再需要手动输入配置路径。

## 运行脚本与 CPU 绑定

新增 `scripts/run.sh`，用于统一运行已安装的程序。

能力：

- 默认执行 `install/bin/minilisysm`。
- 默认使用 `install/etc/minilisysm/lisysm_monitor.ini`。
- 支持 `--config` 指定配置文件。
- 支持 `--cpu` 指定 CPU affinity。
- 默认绑核到 CPU2。

示例：

```bash
./scripts/run.sh
./scripts/run.sh --config ./install/etc/minilisysm/lisysm_monitor.ini
./scripts/run.sh --cpu 1
./scripts/run.sh --cpu 0-3 --config ./my.ini
```

其中 `0-3` 表示允许进程运行在 CPU0、CPU1、CPU2、CPU3 上。

## Agent 运行日志

Agent 启动时补充了运行日志，用于确认：

- 使用了哪个配置文件。
- 配置加载成功。
- 运行时配置摘要。
- 监控程序启动成功。

这样用户不需要等到事件触发后才知道程序是否真的跑起来。

## 事件落盘与人工摘要

围绕事件文件做过一次可读性优化。

当前设计边界：

- JSONL 仍保留，作为机器读取、上传和后续分析的稳定格式。
- 人工查看内容单独写入 summary 文件。
- 机器可读和人工可读输出分目录保存。

目录结构：

```text
lisysm_events/
  jsonl/
    minilisysm-events-YYYYMMDD-HHMMSS-pPID-part000001.jsonl
  summary/
    minilisysm-events-YYYYMMDD-HHMMSS-pPID-part000001.summary.log
```

文件名从 `events_000001.jsonl` 这种不带上下文的形式，调整为包含启动时间、进程号和滚动段号的形式，方便排查多次运行和多进程输出。

人工 summary 的表达方向：

- 中文解释关键含义。
- 保留关键英文原始字段，方便和 JSONL 对照。
- 例如 `当前值(value)`、`警告阈值(warn)`、`严重阈值(crit)`、`统计窗口(window)`、`连续命中(hits)`。
- 状态可表达为 `状态=正在发生/active`。
- 等级字符串从对用户不太直观的 `critical` 调整为 `error`。

曾尝试过颜色和 HTML 方向，但用户要求撤销相关复杂化，最终更偏向普通文本 summary，保证编辑器和终端都能稳定查看。

## Systemd 服务化

新增 systemd 部署入口，用于开机后台自启动：

- `deploy/systemd/minilisysm.service.in`
- `scripts/install_service.sh`
- `scripts/uninstall_service.sh`

使用方式：

```bash
./scripts/install_service.sh
systemctl status minilisysm.service
journalctl -u minilisysm.service -f
```

卸载：

```bash
./scripts/uninstall_service.sh
```

如果修改代码，建议流程：

```bash
./scripts/build.sh
sudo systemctl restart minilisysm.service
systemctl status minilisysm.service
```

systemd 不是唯一开机自启动方式，`/etc/rc.local` 或 init 脚本也可以在部分系统上工作。但对于守护进程，systemd 更适合做状态管理、日志查看、失败重启和开机依赖管理。

## 基础质量门禁

本轮只落地第一批质量门禁，没有引入 spdlog、CPack、Doxygen、Conan/vcpkg 或 Release tag 自动发布，避免一次性改动过大。

新增：

- `.clang-format`
- `.clang-tidy`
- `.github/workflows/ci.yml`
- `scripts/format_check.sh`
- `scripts/lint.sh`
- `scripts/coverage.sh`

CMake 调整：

- 开启 `CMAKE_EXPORT_COMPILE_COMMANDS`，供 clang-tidy 使用。
- 新增 `MINILISYSM_ENABLE_COVERAGE`。
- Debug coverage 构建下为 GCC/Clang 添加 `--coverage`。

依赖脚本调整：

- `scripts/install_deps.sh` 增加 `clang-format`、`clang-tidy`、`lcov`。

CI 设计：

- `format`：运行 clang-format dry-run。
- `lint`：生成 compile database 后运行 clang-tidy。
- `build-test`：覆盖 `ubuntu-22.04`、`ubuntu-24.04`，并覆盖 GCC/Clang。
- `asan-ubsan`：运行 `scripts/verify.sh --asan`。
- `coverage`：运行覆盖率脚本并上传 HTML artifact。

本地质量门禁命令：

```bash
./scripts/format_check.sh
./scripts/lint.sh
./scripts/coverage.sh
```

覆盖率 HTML 产物：

```text
build/coverage-report/index.html
```

## 验证结果

本轮已验证：

```bash
bash scripts/verify.sh
```

结果：

- Release 配置、构建和安装通过。
- 10/10 CTest 单元测试通过。
- SPSC benchmark smoke 通过。

覆盖率编译 smoke：

```bash
cmake -S . -B build/coverage-smoke -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DMINILISYSM_ENABLE_COVERAGE=ON \
  -DMINILISYSM_BUILD_TOOLS=OFF
cmake --build build/coverage-smoke
ctest --test-dir build/coverage-smoke --output-on-failure
```

结果：

- coverage flags 构建通过。
- 10/10 CTest 单元测试通过。

脚本语法检查也已通过：

```bash
bash -n scripts/format_check.sh scripts/lint.sh scripts/coverage.sh \
  scripts/install_deps.sh scripts/build.sh scripts/verify.sh scripts/run.sh
```

本机未完整运行 format、lint 和 HTML coverage 生成，因为当前环境缺少：

- `clang-format`
- `clang-tidy`
- `lcov`
- `genhtml`
- `clang++`

CI 会安装这些工具；本地要完整运行时先执行：

```bash
./scripts/install_deps.sh
```

## 当前边界

- `.clang-tidy` 初次接入后可能暴露历史问题，后续需要决定是修复历史代码，还是先放宽部分规则。
- `.clang-format` 初次接入后也可能需要一次全量格式化提交。
- 覆盖率目前只生成报告，不设置强制覆盖率阈值。
- GitHub Actions 尚未包含 tag release 发布。
- 尚未引入 CPack 标准安装包。
- 尚未引入成熟日志库，Agent 运行日志仍是轻量实现。
- 尚未引入 Doxygen/Sphinx API 文档。

## 后续建议

1. 在本机安装质量门禁依赖后，先跑 `scripts/format_check.sh` 和 `scripts/lint.sh`，确认历史问题规模。
2. 如果格式化差异很多，单独做一次“纯格式化提交”，避免和逻辑变更混在一起。
3. clang-tidy 初次接入建议先按模块修复，不要一次性大改所有文件。
4. 覆盖率先收集基线，再决定核心模块覆盖率目标。
5. 后续再分批推进 spdlog/日志滚动、CPack 打包、Doxygen/Sphinx 文档和 Release pipeline。

## vcpkg 标准依赖管理补充

后续根据标准化依赖管理要求，新增 vcpkg manifest 作为 C++ 第三方依赖入口。

本次完成：

- 新增 `vcpkg.json`，项目名为 `minilisysm`，版本 `0.1.0`。
- 固定 `builtin-baseline` 为 `a0400024711b283056538ac19ced80b91a83c24c`。
- 初始版本 `dependencies` 为空；后续已经引入 `spdlog` 作为第一个真实 C++ 第三方依赖。
- 新增 `cmake/MinilisysmDependencies.cmake`，作为未来 `find_package(...)` 的集中入口。
- 新增 `scripts/bootstrap_vcpkg.sh`，默认使用 `${HOME}/.cache/minilisysm/vcpkg`，支持 `MINILISYSM_VCPKG_ROOT` 覆盖。
- `scripts/build.sh` 增加 `--vcpkg`。
- `scripts/verify.sh` 增加 `--vcpkg`，并支持 `--asan --vcpkg`。
- vcpkg 构建使用独立目录，例如 `build/release-vcpkg`、`install-vcpkg`、`build/asan-vcpkg`、`install-asan-vcpkg`，避免复用普通 CMake cache。
- CI 增加 `vcpkg-manifest` job，运行 `bash scripts/verify.sh --vcpkg`。
- 更新 `README.md`、`docs/DEPENDENCIES.md` 和 `third_party/README.md`，明确 apt/system 脚本负责工具链和系统依赖，vcpkg 负责 C++ 第三方库。

实现细节：

- `scripts/bootstrap_vcpkg.sh` 增加 git 重试逻辑。
- clone 使用 `--filter=blob:none --no-checkout`。
- git 命令使用 `http.version=HTTP/1.1`，因为首次访问 GitHub 时出现过 TLS 连接提前终止。

已验证：

```bash
bash -n scripts/bootstrap_vcpkg.sh scripts/build.sh scripts/verify.sh
python3 -m json.tool vcpkg.json
./scripts/format_check.sh
bash scripts/verify.sh
bash scripts/bootstrap_vcpkg.sh
bash scripts/verify.sh --vcpkg
bash scripts/verify.sh --asan --vcpkg
```

验证结果：

- 普通 Release：10/10 CTest 通过。
- vcpkg Release：10/10 CTest 通过。
- ASan + vcpkg：10/10 CTest 通过。

边界：

- 没有迁移业务代码到第三方库。
- eBPF 依赖仍保留系统依赖路线，包括 `libbpf`、`bpftool`、kernel headers 和 `clang -target bpf`。
- 后续新增 C++ 第三方库时，应优先修改 `vcpkg.json` 和 `cmake/MinilisysmDependencies.cmake`，不要把第三方源码散落到业务目录。

## spdlog 工业级日志框架补充

在 vcpkg 依赖管理骨架之后，`spdlog` 已作为第一个真实 C++ 第三方依赖接入。

本次完成：

- `vcpkg.json` 的 `dependencies` 从空列表变为包含 `spdlog`。
- `cmake/MinilisysmDependencies.cmake` 查找 `spdlog`，找不到时给出明确错误提示。
- `minilisysm` 主程序链接 `spdlog::spdlog`。
- `apps/minilisysm-agent/main.cpp` 移除手写 `log_line` / `std::cerr` 拼接日志，改为 spdlog。
- 启动早期先使用控制台 bootstrap logger，配置加载后切换为异步 spdlog logger。
- 支持 `debug`、`info`、`warn`、`error` 分级。
- 支持控制台输出、按大小滚动文件、按天滚动文件和 null sink。
- 默认日志文件为 `./lisysm_logs/minilisysm-agent.log`。
- 默认按大小滚动：`rotate_mb=16`、`rotate_files=8`。
- 默认异步队列大小为 `8192`。
- 新增 `[agent_log]` 配置段。
- `scripts/build.sh` 和 `scripts/verify.sh` 默认使用 vcpkg，保留 `--no-vcpkg` 供系统包依赖环境使用。
- `scripts/run.sh` 默认运行 `install-vcpkg/`，保留 `--no-vcpkg` 运行旧的 `install/`。
- `lisysm_logs/` 加入 `.gitignore`。

新增默认配置：

```ini
[agent_log]
enable=true
level=info
console=true
path=./lisysm_logs/minilisysm-agent.log
rotation=size
rotate_mb=16
rotate_files=8
async_queue_size=8192
```

已验证：

```bash
./scripts/format_check.sh
bash -n scripts/bootstrap_vcpkg.sh scripts/build.sh scripts/verify.sh scripts/lint.sh scripts/coverage.sh scripts/install_deps.sh scripts/run.sh
python3 -m json.tool vcpkg.json
```

当前验证边界：

- 本机完整 `bash scripts/verify.sh` 被环境依赖阻塞。
- vcpkg 构建 spdlog 的传递依赖 `fmt` 时需要 `pkg-config`。
- 当前机器缺少 `pkg-config`，且 `./scripts/install_deps.sh` 调用 sudo 时需要交互密码，无法在本会话中安装。
- 已将 `pkg-config` 纳入 `scripts/install_deps.sh`，并让 `scripts/bootstrap_vcpkg.sh` 提前检查 `pkg-config`，避免用户看到很长的 vcpkg 深层错误。

## spdlog 使用范围扩展补充

用户指出只在 `apps/minilisysm-agent/main.cpp` 使用 spdlog 范围偏少，随后将 spdlog 扩展到核心运行链路。

本次扩展：

- `minilisysm_core` 改为 `PUBLIC` 链接 `spdlog::spdlog`，让 runtime/storage 等核心模块可以直接使用 spdlog。
- `minilisysm` 主程序只链接 `minilisysm_core`，避免重复声明依赖。
- `Monitor` 增加生命周期日志：启动、停止、worker 线程启动、dispatcher/metrics 启动失败。
- `Monitor` 增加线程策略失败日志：CPU affinity 和 nice 设置失败。
- `Monitor` 增加 collector overrun 日志和 collector failure 日志。
- `Monitor::publish_event()` 在 source queue push 失败时记录 warning。
- `EventDispatcher` 增加 dispatcher start/stop、线程策略失败和 sink queue push 失败日志。
- sink queue push 失败做限频：第一次和每 1000 次记录一次 warning。
- `EventDispatcherGroup` 增加 group/sink start/stop 以及启动失败日志。
- `JsonlEventSink` 增加启停、文件打开、写入失败、summary 打开/写入失败、文件滚动、fsync 失败、缓存清理日志。
- `NetworkEventSink` 增加启停、endpoint 无效、WAL 加载、WAL segment 滚动、WAL 写入/重写失败、flush 失败、发送失败、非 2xx 响应、WAL 超限日志。
- WAL 超限丢弃非 critical 事件做限频：第一次和每 1000 次记录一次 warning。
- `MetricsServer` 增加启停、socket/bind/listen 失败、accept/send debug 日志。

设计约束：

- 不在每条普通事件或每轮采集循环上打 info 日志。
- 高频失败路径只做限频 warning 或 debug，避免日志系统本身扩大故障。
- spdlog 初始化仍只在 Agent 入口完成，核心模块只使用默认 logger，不自己初始化 logger。

扩展后已验证：

```bash
./scripts/format_check.sh
bash -n scripts/bootstrap_vcpkg.sh scripts/build.sh scripts/verify.sh scripts/lint.sh scripts/coverage.sh scripts/install_deps.sh scripts/run.sh
python3 -m json.tool vcpkg.json
```

完整编译验证仍受当前机器缺少 `pkg-config` 阻塞，需要先运行：

```bash
./scripts/install_deps.sh
```

## clang-tidy main.cpp 修复补充

用户运行 clang-tidy 时反馈 `apps/minilisysm-agent/main.cpp` 中两个 error：

- `g_stop` 是非 const 全局可访问变量，触发 `cppcoreguidelines-avoid-non-const-global-variables`。
- `argv[1]` 触发 `cppcoreguidelines-pro-bounds-pointer-arithmetic`。

修复：

- 删除全局 `g_stop`，改为 `stop_requested()` 返回函数内静态 `std::atomic<bool>`。
- 在注册 signal handler 前主动初始化 `stop_requested()`。
- 将 `argv[1]` 改为 `std::string(*std::next(argv))`。
- 顺手将 spdlog daily/size rotation 分支拆开，避免 `bugprone-branch-clone` 潜在误报。
- `sinks` 容器显式初始化为 `{}`。

已验证：

```bash
./scripts/format_check.sh
```

当前仍无法完整跑 clang-tidy，因为本机缺 `pkg-config`，vcpkg 尚未完成 spdlog/fmt 依赖安装。

## spdlog 实测验证补充

后续本机已具备 `pkg-config`，因此完成了 spdlog 的完整构建与运行验证：

- `bash scripts/bootstrap_vcpkg.sh` 成功 checkout 到 baseline `a0400024711b283056538ac19ced80b91a83c24c` 并完成 bootstrap。
- `bash scripts/verify.sh` 成功走 vcpkg 构建路径，vcpkg 确认安装 `spdlog[core,tz-offset,fmt]:arm64-linux@1.17.0`。
- `build/release-vcpkg` 下 10 个 CTest 单元测试全部通过。
- 冒烟测试使用 `install-vcpkg/bin/minilisysm` 和临时配置启动。由于系统已有旧版 `minilisysm` 占用 9108 端口，测试配置临时关闭 metrics。
- spdlog 成功生成 `/tmp/minilisysm-spdlog-logs/agent.log`。
- 日志文件中确认出现 `config loaded`、`monitor started successfully`、`stop signal received`、`monitor stopped` 等关键生命周期信息。
- 兼容性修复：vcpkg 当前 spdlog 1.17.0 没有 `spdlog/sinks/stderr_color_sinks.h`，agent 改为包含 `spdlog/sinks/stdout_color_sinks.h` 并使用 `stdout_color_sink_mt`。

## 日志目录结构整理补充

用户反馈根目录下 `lisysm_logs`、`lisysm_events`、`lisysm_wal` 等运行产物目录分散，要求所有 log 放到一个 log 文件夹下。

本次整理为统一的 `./logs/` 结构：

- Agent 自身运行日志：`./logs/agent/minilisysm-agent.log`。
- 事件缓存根目录：`./logs/events`。
- 机器读取 JSONL：`./logs/events/jsonl`。
- 人工查看 summary：`./logs/events/summary`。
- 网络 WAL：`./logs/wal`。

改动范围：

- 更新 `configs/lisysm_monitor.ini` 的 `[agent_log] path`、`[persistence] cache_path`、`[network_sink] wal_path`。
- 更新 `include/minilisysm/core/config.hpp` 中对应默认值，避免未传配置时仍落到旧目录。
- 更新 `.gitignore`，忽略 `logs/*` 但保留 `logs/.gitkeep`，同时继续忽略历史旧目录。
- 更新 `README.md` 和 `docs/DEPLOYMENT.md` 的运行路径说明。
- 新增 `logs/.gitkeep`，让统一日志根目录在仓库中可见。

验证：

```bash
./scripts/format_check.sh
bash -n scripts/build.sh scripts/verify.sh scripts/run.sh scripts/install_service.sh scripts/uninstall_service.sh
bash scripts/verify.sh
```

运行时 smoke 已确认生成：

```text
/tmp/minilisysm-log-layout/logs/agent/minilisysm-agent.log
/tmp/minilisysm-log-layout/logs/events/jsonl/minilisysm-events-*.jsonl
/tmp/minilisysm-log-layout/logs/events/summary/minilisysm-events-*.summary.log
```

旧的 `lisysm_logs/`、`lisysm_events/`、`lisysm_events_event_test/` 未自动删除，避免误删用户之前的测试证据。

## 旧日志目录清理补充

用户随后明确要求删除旧目录，因此已清理项目根目录下旧的运行产物目录：

- `lisysm_events/`
- `lisysm_events_event_test/`
- `lisysm_logs/`
- `lisysm_wal/` 如果存在

清理后确认旧目录不再存在，新的 `logs/` 根目录保留。

## install 目录收口补充

用户指出根目录生成 `install/`、`install-vcpkg/`、`install-asan-vcpkg/` 等多份安装目录不够工程化。

原因：

- 之前为了避免 vcpkg/no-vcpkg、Release/ASan/eBPF 变体互相覆盖，简单地把每种构建变体映射到一个根目录 install 名称。
- 这个方案虽然隔离了产物，但让项目根目录变乱，也让 systemd 安装脚本固定检查 `install/bin/minilisysm` 时与默认 vcpkg 构建产物不一致。

新策略：

- 正式可运行/可分发产物统一安装到 `install/`。
- `--vcpkg` 和 `--no-vcpkg` 只表示依赖来源，不再改变 install prefix。
- `--ebpf` 是正式能力变体，构建后也安装到 `install/`。
- `--asan` 是测试/插桩产物，安装到构建目录内部，例如 `build/asan-vcpkg/install/` 或 `build/asan/install/`，不污染项目根目录。
- `scripts/run.sh` 默认运行 `install/`，传 `--asan` 时才运行嵌套 ASan install。
- 清理了根目录历史遗留的 `install-vcpkg/`、`install-asan-vcpkg/` 等目录。

验证：

```bash
./scripts/format_check.sh
bash -n scripts/build.sh scripts/verify.sh scripts/run.sh scripts/install_service.sh scripts/uninstall_service.sh
bash scripts/verify.sh
bash scripts/build.sh --asan
bash scripts/verify.sh --asan
```

结果：

- 默认 Release/vcpkg 构建安装到 `install/`。
- ASan 构建安装到 `build/asan-vcpkg/install/`。
- Release 和 ASan 两套验证均 10 个 CTest 全部通过。
