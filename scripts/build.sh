#!/usr/bin/env bash
set -euo pipefail

ENABLE_ASAN=0
ENABLE_EBPF=0

usage() {
    cat <<'EOF'
Usage: scripts/build.sh [--asan | --ebpf]

Builds minilisysm using Ninja.

Options:
  --asan      Configure and build build-asan with ASan/UBSan flags.
  --ebpf      Configure and build build-ebpf with MINILISYSM_ENABLE_EBPF=ON.
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

BUILD_DIR="build"
CMAKE_ARGS=(-S . -B "${BUILD_DIR}" -G Ninja -DCMAKE_BUILD_TYPE=Release)

if [[ "${ENABLE_ASAN}" -eq 1 ]]; then
    BUILD_DIR="build-asan"
    CMAKE_ARGS=(
        -S . -B "${BUILD_DIR}" -G Ninja
        -DCMAKE_BUILD_TYPE=Debug
        -DCMAKE_CXX_FLAGS=-fsanitize=address,undefined\ -fno-omit-frame-pointer
        -DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined
    )
elif [[ "${ENABLE_EBPF}" -eq 1 ]]; then
    BUILD_DIR="build-ebpf"
    if grep -qi microsoft /proc/version 2>/dev/null && [[ "${ROOT_DIR}" == /mnt/* ]]; then
        BUILD_DIR="/tmp/minilisysm-build-ebpf"
        rm -rf "${BUILD_DIR}"
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
    CMAKE_ARGS=(-S . -B "${BUILD_DIR}" -G Ninja -DCMAKE_BUILD_TYPE=Release -DMINILISYSM_ENABLE_EBPF=ON -DBPFTOOL_EXECUTABLE="${BPFTOOL_PATH}")
fi

cmake "${CMAKE_ARGS[@]}"
cmake --build "${BUILD_DIR}"

echo "Build directory: ${BUILD_DIR}"
echo "Binary: ${BUILD_DIR}/minilisysm"
