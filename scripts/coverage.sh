#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${ROOT_DIR}"

BUILD_DIR="${MINILISYSM_COVERAGE_BUILD_DIR:-build/coverage}"
REPORT_DIR="${MINILISYSM_COVERAGE_REPORT_DIR:-build/coverage-report}"
RAW_INFO="${BUILD_DIR}/coverage.raw.info"
FILTERED_INFO="${BUILD_DIR}/coverage.info"
VCPKG_OUTPUT="$(bash "${ROOT_DIR}/scripts/bootstrap_vcpkg.sh")"
VCPKG_TOOLCHAIN_FILE="$(printf '%s\n' "${VCPKG_OUTPUT}" | awk -F= '/^CMAKE_TOOLCHAIN_FILE=/ {print $2}' | tail -1)"

for tool in cmake ninja ctest lcov genhtml gcov; do
    if ! command -v "${tool}" >/dev/null 2>&1; then
        echo "${tool} is required for coverage. Install with: sudo apt-get install -y lcov" >&2
        exit 1
    fi
done

cmake \
    -S . -B "${BUILD_DIR}" -G Ninja \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_TOOLCHAIN_FILE="${VCPKG_TOOLCHAIN_FILE}" \
    -DMINILISYSM_ENABLE_COVERAGE=ON \
    -DMINILISYSM_BUILD_TOOLS=OFF

cmake --build "${BUILD_DIR}"
ctest --test-dir "${BUILD_DIR}" --output-on-failure

lcov --capture --directory "${BUILD_DIR}" --output-file "${RAW_INFO}"
lcov \
    --remove "${RAW_INFO}" \
    '/usr/*' \
    '*/tests/*' \
    '*/build/*' \
    '*/generated/*' \
    --output-file "${FILTERED_INFO}"

rm -rf "${REPORT_DIR}"
genhtml "${FILTERED_INFO}" --output-directory "${REPORT_DIR}" --title "minilisysm coverage"

echo "Coverage report: ${REPORT_DIR}/index.html"
