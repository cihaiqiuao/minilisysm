#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${ROOT_DIR}"

if ! command -v clang-format >/dev/null 2>&1; then
    echo "clang-format is required. Install it with: sudo apt-get install -y clang-format" >&2
    exit 1
fi

mapfile -t FILES < <(
    git ls-files \
        'apps/**/*.cpp' 'apps/**/*.hpp' 'apps/**/*.h' \
        'include/**/*.cpp' 'include/**/*.hpp' 'include/**/*.h' \
        'src/**/*.cpp' 'src/**/*.hpp' 'src/**/*.h' \
        'tests/**/*.cpp' 'tests/**/*.hpp' 'tests/**/*.h' \
        'tools/**/*.cpp' 'tools/**/*.hpp' 'tools/**/*.h' \
        'ebpf/include/**/*.cpp' 'ebpf/include/**/*.hpp' 'ebpf/include/**/*.h' \
        'ebpf/src/**/*.cpp' 'ebpf/src/**/*.hpp' 'ebpf/src/**/*.h' \
        'ebpf/bpf/**/*.c' 'ebpf/bpf/**/*.h'
)

if [[ "${#FILES[@]}" -eq 0 ]]; then
    echo "No C/C++ files found."
    exit 0
fi

clang-format --dry-run --Werror "${FILES[@]}"
echo "clang-format check passed for ${#FILES[@]} files."
