# Central entrypoint for C++ third-party dependencies.
#
# Linux/eBPF system dependencies such as libbpf stay near the optional eBPF
# block because they are tied to the target host kernel/toolchain.

find_package(spdlog CONFIG QUIET)
if(NOT spdlog_FOUND)
    message(FATAL_ERROR "spdlog is required. Use scripts/build.sh or scripts/verify.sh so vcpkg can provide it, or install a system spdlog package and pass --no-vcpkg.")
endif()
