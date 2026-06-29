#!/usr/bin/env bash
set -euo pipefail

CONFIG_PATH=""
CPU_SET="2"
USE_VCPKG=1
VARIANT="release"

usage() {
    cat <<'EOF'
Usage: scripts/run.sh [options] [-- extra-args...]

Runs the installed minilisysm agent.

Options:
  -c, --config PATH  Use a specific config file instead of the installed default.
  --cpu CPUSET       Bind the process with taskset. Default: 2.
  --asan             Run the ASan test install under build/asan[-vcpkg]/install.
  --ebpf             Run install/ after building with ./scripts/build.sh --ebpf.
  --vcpkg            Select the vcpkg ASan build directory when used with --asan. Default.
  --no-vcpkg         Select the non-vcpkg ASan build directory when used with --asan.
  -h, --help         Show this help.

Examples:
  ./scripts/run.sh
  ./scripts/run.sh --config ./install/etc/minilisysm/lisysm_monitor.ini
  ./scripts/run.sh --cpu 1
  ./scripts/run.sh --cpu 0-3 --config ./my.ini
EOF
}

while (($#)); do
    case "$1" in
        -c|--config)
            if [[ $# -lt 2 ]]; then
                echo "$1 requires a path." >&2
                exit 2
            fi
            CONFIG_PATH="$2"
            shift 2
            ;;
        --cpu)
            if [[ $# -lt 2 ]]; then
                echo "--cpu requires a CPU set, for example 2 or 0-3." >&2
                exit 2
            fi
            CPU_SET="$2"
            shift 2
            ;;
        --asan)
            VARIANT="asan"
            shift
            ;;
        --ebpf)
            VARIANT="ebpf"
            shift
            ;;
        --vcpkg)
            USE_VCPKG=1
            shift
            ;;
        --no-vcpkg)
            USE_VCPKG=0
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        --)
            shift
            break
            ;;
        *)
            echo "unknown option: $1" >&2
            usage >&2
            exit 2
            ;;
    esac
done

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${ROOT_DIR}"

case "${VARIANT}:${USE_VCPKG}" in
    release:*) INSTALL_DIR="install" ;;
    ebpf:*) INSTALL_DIR="install" ;;
    asan:1) INSTALL_DIR="build/asan-vcpkg/install" ;;
    asan:0) INSTALL_DIR="build/asan/install" ;;
esac

BINARY="${ROOT_DIR}/${INSTALL_DIR}/bin/minilisysm"
if [[ ! -x "${BINARY}" ]]; then
    echo "binary not found or not executable: ${BINARY}" >&2
    echo "Run ./scripts/build.sh first." >&2
    exit 1
fi

CMD=("${BINARY}")
if [[ -n "${CONFIG_PATH}" ]]; then
    if [[ ! -f "${CONFIG_PATH}" ]]; then
        echo "config file not found: ${CONFIG_PATH}" >&2
        exit 1
    fi
    CMD+=("${CONFIG_PATH}")
fi

if [[ $# -gt 0 ]]; then
    CMD+=("$@")
fi

echo "$(date '+%Y-%m-%d %H:%M:%S') [info] run: binary=${BINARY}" >&2
echo "$(date '+%Y-%m-%d %H:%M:%S') [info] run: install_dir=${INSTALL_DIR}" >&2
if [[ -n "${CONFIG_PATH}" ]]; then
    echo "$(date '+%Y-%m-%d %H:%M:%S') [info] run: config=${CONFIG_PATH}" >&2
else
    echo "$(date '+%Y-%m-%d %H:%M:%S') [info] run: config=default" >&2
fi

if [[ -n "${CPU_SET}" ]]; then
    if ! command -v taskset >/dev/null 2>&1; then
        echo "taskset is required for --cpu. Install util-linux first." >&2
        exit 1
    fi
    echo "$(date '+%Y-%m-%d %H:%M:%S') [info] run: cpu_set=${CPU_SET}" >&2
    exec taskset -c "${CPU_SET}" "${CMD[@]}"
fi

echo "$(date '+%Y-%m-%d %H:%M:%S') [info] run: cpu_set=none" >&2
exec "${CMD[@]}"
