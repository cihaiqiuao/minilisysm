#!/usr/bin/env bash
set -euo pipefail

WITH_EBPF=0

usage() {
    cat <<'EOF'
Usage: scripts/install_deps.sh [--with-ebpf]

Installs Ubuntu/WSL build dependencies for minilisysm.

Options:
  --with-ebpf   Also install optional eBPF build dependencies.
  -h, --help    Show this help.
EOF
}

while (($#)); do
    case "$1" in
        --with-ebpf)
            WITH_EBPF=1
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

if ! command -v apt-get >/dev/null 2>&1; then
    echo "install_deps.sh currently supports Ubuntu/WSL systems with apt-get only." >&2
    exit 1
fi

SUDO=()
if [[ "${EUID}" -ne 0 ]]; then
    if ! command -v sudo >/dev/null 2>&1; then
        echo "sudo is required when not running as root." >&2
        exit 1
    fi
    SUDO=(sudo)
fi

BASE_PACKAGES=(
    build-essential
    clang-format
    clang-tidy
    cmake
    curl
    git
    lcov
    ninja-build
    pkg-config
    tar
    unzip
    zip
)

EBPF_PACKAGES=(
    clang
    llvm
    libbpf-dev
)

"${SUDO[@]}" apt-get update
"${SUDO[@]}" apt-get install -y "${BASE_PACKAGES[@]}"

if [[ "${WITH_EBPF}" -eq 1 ]]; then
    "${SUDO[@]}" apt-get install -y "${EBPF_PACKAGES[@]}"
    if ! "${SUDO[@]}" apt-get install -y bpftool; then
        echo "warning: direct bpftool package is unavailable; trying linux-tools-common fallback." >&2
        "${SUDO[@]}" apt-get install -y linux-tools-common
    fi
    if ! command -v bpftool >/dev/null 2>&1 || ! bpftool version >/dev/null 2>&1; then
        TOOLS_PACKAGE="$(apt-cache search '^linux-tools-[0-9].*-generic$' | awk '{print $1}' | sort -V | tail -1)"
        if [[ -n "${TOOLS_PACKAGE}" ]]; then
            echo "installing fallback bpftool provider: ${TOOLS_PACKAGE}" >&2
            "${SUDO[@]}" apt-get install -y "${TOOLS_PACKAGE}"
        fi
    fi
    if ! command -v bpftool >/dev/null 2>&1 && ! find /usr/lib/linux-tools-* -type f -name bpftool >/dev/null 2>&1; then
        echo "warning: no runnable bpftool found; set BPFTOOL=/path/to/bpftool before scripts/build.sh --ebpf." >&2
    fi
    HEADER_PACKAGE="linux-headers-$(uname -r)"
    if ! "${SUDO[@]}" apt-get install -y "${HEADER_PACKAGE}"; then
        echo "warning: ${HEADER_PACKAGE} is unavailable; base and user-space eBPF skeleton builds can still proceed." >&2
    fi
fi
