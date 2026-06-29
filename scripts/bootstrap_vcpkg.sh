#!/usr/bin/env bash
set -euo pipefail

VCPKG_BASELINE="a0400024711b283056538ac19ced80b91a83c24c"
VCPKG_REPO="https://github.com/microsoft/vcpkg.git"
VCPKG_ROOT="${MINILISYSM_VCPKG_ROOT:-${HOME}/.cache/minilisysm/vcpkg}"

usage() {
    cat <<'EOF'
Usage: scripts/bootstrap_vcpkg.sh

Bootstraps the pinned vcpkg checkout used by minilisysm.

Environment:
  MINILISYSM_VCPKG_ROOT  Override vcpkg checkout path.
EOF
}

while (($#)); do
    case "$1" in
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
done

for tool in git curl pkg-config zip unzip tar; do
    if ! command -v "${tool}" >/dev/null 2>&1; then
        echo "${tool} is required to bootstrap vcpkg." >&2
        exit 1
    fi
done

retry_git() {
    local attempt=1
    while true; do
        if git -c http.version=HTTP/1.1 "$@"; then
            return 0
        fi
        if [[ "${attempt}" -ge 3 ]]; then
            return 1
        fi
        echo "git command failed; retrying (${attempt}/3)..." >&2
        sleep $((attempt * 2))
        attempt=$((attempt + 1))
    done
}

if [[ ! -d "${VCPKG_ROOT}/.git" ]]; then
    rm -rf "${VCPKG_ROOT}"
    mkdir -p "$(dirname "${VCPKG_ROOT}")"
    retry_git clone --filter=blob:none --no-checkout "${VCPKG_REPO}" "${VCPKG_ROOT}"
fi

retry_git -C "${VCPKG_ROOT}" fetch --depth 1 origin "${VCPKG_BASELINE}"
retry_git -C "${VCPKG_ROOT}" checkout --detach "${VCPKG_BASELINE}"

"${VCPKG_ROOT}/bootstrap-vcpkg.sh" -disableMetrics

echo "VCPKG_ROOT=${VCPKG_ROOT}"
echo "CMAKE_TOOLCHAIN_FILE=${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
