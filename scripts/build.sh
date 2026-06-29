#!/usr/bin/env bash
set -euo pipefail

ENABLE_ASAN=0
ENABLE_EBPF=0
ENABLE_VCPKG=1

usage() {
    cat <<'EOF'
Usage: scripts/build.sh [--asan | --ebpf] [--vcpkg | --no-vcpkg]

Builds minilisysm using Ninja.

Options:
  --asan      Configure an ASan/UBSan test build under build/asan[-vcpkg].
  --ebpf      Configure an eBPF-enabled release build with MINILISYSM_ENABLE_EBPF=ON.
  --vcpkg     Configure CMake with the pinned vcpkg manifest toolchain (default).
  --no-vcpkg  Use system-provided C++ dependencies instead of vcpkg.
  -h, --help  Show this help.
EOF
}

while (($#)); do
    case "$1" in
        --asan)
            ENABLE_ASAN=1
            ;;
        --ebpf)
            ENABLE_EBPF=1
            ;;
        --vcpkg)
            ENABLE_VCPKG=1
            ;;
        --no-vcpkg)
            ENABLE_VCPKG=0
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "unknown option: $1" >&2
            usage >&2
            exit 2
            ;;
    esac
    shift
done

if [[ "${ENABLE_ASAN}" -eq 1 && "${ENABLE_EBPF}" -eq 1 ]]; then
    echo "--asan and --ebpf must be built separately." >&2
    exit 2
fi

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${ROOT_DIR}"

BUILD_DIR="build/release"
INSTALL_DIR="install"
BUILD_TYPE="Release"
CMAKE_EXTRA_ARGS=()
IS_TEST_INSTALL=0

if [[ "${ENABLE_ASAN}" -eq 1 ]]; then
    BUILD_DIR="build/asan"
    BUILD_TYPE="Debug"
    IS_TEST_INSTALL=1
    CMAKE_EXTRA_ARGS+=(
        -DCMAKE_CXX_FLAGS=-fsanitize=address,undefined\ -fno-omit-frame-pointer
        -DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined
    )
elif [[ "${ENABLE_EBPF}" -eq 1 ]]; then
    BUILD_DIR="build/ebpf"
    if grep -qi microsoft /proc/version 2>/dev/null && [[ "${ROOT_DIR}" == /mnt/* ]]; then
        BUILD_DIR="/tmp/minilisysm-build-ebpf"
    fi
    BPFTOOL_PATH="${BPFTOOL:-}"
    if [[ -z "${BPFTOOL_PATH}" ]] || ! "${BPFTOOL_PATH}" version >/dev/null 2>&1; then
        if command -v bpftool >/dev/null 2>&1 && bpftool version >/dev/null 2>&1; then
            BPFTOOL_PATH="$(command -v bpftool)"
        else
            BPFTOOL_PATH="$(find /usr/lib/linux-tools-* -type f -name bpftool 2>/dev/null | sort -V | tail -1 || true)"
        fi
    fi
    if [[ -z "${BPFTOOL_PATH}" ]] || ! "${BPFTOOL_PATH}" version >/dev/null 2>&1; then
        echo "bpftool is required for --ebpf. Install linux-tools for your distro or set BPFTOOL=/path/to/bpftool." >&2
        exit 1
    fi
    echo "Using bpftool: ${BPFTOOL_PATH}"
    CMAKE_EXTRA_ARGS+=(
        -DMINILISYSM_ENABLE_EBPF=ON
        -DBPFTOOL_EXECUTABLE="${BPFTOOL_PATH}"
    )
fi

if [[ "${ENABLE_VCPKG}" -eq 1 ]]; then
    if [[ "${ENABLE_ASAN}" -eq 1 ]]; then
        BUILD_DIR="build/asan-vcpkg"
    elif [[ "${ENABLE_EBPF}" -eq 1 ]]; then
        if [[ "${BUILD_DIR}" == /tmp/minilisysm-build-ebpf ]]; then
            BUILD_DIR="/tmp/minilisysm-build-ebpf-vcpkg"
        else
            BUILD_DIR="build/ebpf-vcpkg"
        fi
    else
        BUILD_DIR="build/release-vcpkg"
    fi

    VCPKG_OUTPUT="$(bash "${ROOT_DIR}/scripts/bootstrap_vcpkg.sh")"
    echo "${VCPKG_OUTPUT}"
    VCPKG_TOOLCHAIN_FILE="$(printf '%s\n' "${VCPKG_OUTPUT}" | awk -F= '/^CMAKE_TOOLCHAIN_FILE=/ {print $2}' | tail -1)"
    if [[ -z "${VCPKG_TOOLCHAIN_FILE}" || ! -f "${VCPKG_TOOLCHAIN_FILE}" ]]; then
        echo "failed to resolve vcpkg CMAKE_TOOLCHAIN_FILE." >&2
        exit 1
    fi
    CMAKE_EXTRA_ARGS+=("-DCMAKE_TOOLCHAIN_FILE=${VCPKG_TOOLCHAIN_FILE}")
fi

if [[ "${BUILD_DIR}" == /tmp/minilisysm-build-ebpf* ]]; then
    rm -rf "${BUILD_DIR}"
fi

if [[ "${IS_TEST_INSTALL}" -eq 1 ]]; then
    INSTALL_DIR="${BUILD_DIR}/install"
else
    INSTALL_DIR="install"
fi

CMAKE_ARGS=(
    -S . -B "${BUILD_DIR}" -G Ninja
    -DCMAKE_BUILD_TYPE="${BUILD_TYPE}"
    -DCMAKE_INSTALL_PREFIX="${ROOT_DIR}/${INSTALL_DIR}"
    "${CMAKE_EXTRA_ARGS[@]}"
)

cmake "${CMAKE_ARGS[@]}"
cmake --build "${BUILD_DIR}"
cmake --install "${BUILD_DIR}"

echo "Build directory: ${BUILD_DIR}"
echo "Install directory: ${INSTALL_DIR}"
echo "Binary: ${INSTALL_DIR}/bin/minilisysm"
echo "Config: ${INSTALL_DIR}/etc/minilisysm/lisysm_monitor.ini"
