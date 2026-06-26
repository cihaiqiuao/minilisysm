#!/usr/bin/env bash
set -euo pipefail

ENABLE_ASAN=0
ENABLE_EBPF=0

usage() {
    cat <<'EOF'
Usage: scripts/verify.sh [--asan | --ebpf]

Builds and verifies minilisysm.

Options:
  --asan      Verify the ASan/UBSan build.
  --ebpf      Verify the optional eBPF user-space skeleton build.
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
    echo "--asan and --ebpf must be verified separately." >&2
    exit 2
fi

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${ROOT_DIR}"

BUILD_ARG=()
BUILD_DIR="build"

if [[ "${ENABLE_ASAN}" -eq 1 ]]; then
    BUILD_ARG=(--asan)
    BUILD_DIR="build-asan"
elif [[ "${ENABLE_EBPF}" -eq 1 ]]; then
    BUILD_ARG=(--ebpf)
    BUILD_DIR="build-ebpf"
    if grep -qi microsoft /proc/version 2>/dev/null && [[ "${ROOT_DIR}" == /mnt/* ]]; then
        BUILD_DIR="/tmp/minilisysm-build-ebpf"
    fi
fi

bash "${ROOT_DIR}/scripts/build.sh" "${BUILD_ARG[@]}"
ctest --test-dir "${BUILD_DIR}" --output-on-failure

if [[ "${ENABLE_ASAN}" -eq 0 && "${ENABLE_EBPF}" -eq 0 ]]; then
    "./${BUILD_DIR}/bench_spsc"
fi

if [[ "${ENABLE_EBPF}" -eq 1 ]]; then
    echo "eBPF build directory: ${BUILD_DIR}"
    if [[ "${EUID}" -ne 0 ]]; then
        echo "Skipping eBPF runtime smoke because loading BPF programs requires root/sudo."
        exit 0
    fi

    SMOKE_CONFIG="/tmp/minilisysm-ebpf-smoke.ini"
    SMOKE_EVENTS="/tmp/minilisysm-ebpf-events"
    rm -rf "${SMOKE_EVENTS}"
    sed \
        -e 's/^source=.*/source=ebpf/' \
        -e "s#^cache_path=.*#cache_path=${SMOKE_EVENTS}#" \
        configs/lisysm_monitor.ini > "${SMOKE_CONFIG}"

    timeout 3s "${BUILD_DIR}/minilisysm" "${SMOKE_CONFIG}" || code=$?
    code="${code:-0}"
    if [[ "${code}" -ne 0 && "${code}" -ne 124 ]]; then
        exit "${code}"
    fi
    test -f "${SMOKE_EVENTS}/events_000001.jsonl"
    echo "eBPF smoke wrote ${SMOKE_EVENTS}/events_000001.jsonl"
fi
