#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${ROOT_DIR}"

BUILD_DIR="${MINILISYSM_LINT_BUILD_DIR:-build/release-vcpkg}"
WARNINGS_AS_ERRORS="${MINILISYSM_TIDY_WARNINGS_AS_ERRORS:-bugprone-*,-bugprone-easily-swappable-parameters,-bugprone-implicit-widening-of-multiplication-result,performance-*,readability-implicit-bool-conversion}"
VCPKG_OUTPUT="$(bash "${ROOT_DIR}/scripts/bootstrap_vcpkg.sh")"
VCPKG_TOOLCHAIN_FILE="$(printf '%s\n' "${VCPKG_OUTPUT}" | awk -F= '/^CMAKE_TOOLCHAIN_FILE=/ {print $2}' | tail -1)"

if ! command -v clang-tidy >/dev/null 2>&1; then
    echo "clang-tidy is required. Install it with: sudo apt-get install -y clang-tidy" >&2
    exit 1
fi
if ! command -v cmake >/dev/null 2>&1 || ! command -v ninja >/dev/null 2>&1; then
    echo "cmake and ninja are required before running clang-tidy." >&2
    exit 1
fi

if [[ ! -f "${BUILD_DIR}/compile_commands.json" ]]; then
    cmake \
        -S . -B "${BUILD_DIR}" -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_TOOLCHAIN_FILE="${VCPKG_TOOLCHAIN_FILE}"
fi

mapfile -t FILES < <(
    git ls-files \
        'apps/**/*.cpp' \
        'src/**/*.cpp' \
        'tests/**/*.cpp' \
        'tools/**/*.cpp'
)

if [[ "${#FILES[@]}" -eq 0 ]]; then
    echo "No C++ translation units found."
    exit 0
fi

ARGS=(-p "${BUILD_DIR}" --quiet)
if [[ -n "${WARNINGS_AS_ERRORS}" ]]; then
    ARGS+=("-warnings-as-errors=${WARNINGS_AS_ERRORS}")
fi

for file in "${FILES[@]}"; do
    clang-tidy "${ARGS[@]}" "${file}"
done

echo "clang-tidy passed for ${#FILES[@]} translation units."
