#!/usr/bin/env bash
set -euo pipefail

SERVICE_NAME="minilisysm"

usage() {
    cat <<'EOF'
Usage: scripts/uninstall_service.sh [--name NAME]

Stops, disables, and removes the systemd service installed by install_service.sh.

Options:
  --name NAME   Service name. Default: minilisysm
  -h, --help    Show this help.
EOF
}

while (($#)); do
    case "$1" in
        --name)
            if [[ $# -lt 2 ]]; then
                echo "--name requires a value." >&2
                exit 2
            fi
            SERVICE_NAME="$2"
            shift 2
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
done

SUDO=()
if [[ "${EUID}" -ne 0 ]]; then
    if ! command -v sudo >/dev/null 2>&1; then
        echo "sudo is required when not running as root." >&2
        exit 1
    fi
    SUDO=(sudo)
fi

"${SUDO[@]}" systemctl disable --now "${SERVICE_NAME}.service" || true
"${SUDO[@]}" rm -f "/etc/systemd/system/${SERVICE_NAME}.service"
"${SUDO[@]}" systemctl daemon-reload
"${SUDO[@]}" systemctl reset-failed "${SERVICE_NAME}.service" || true

echo "Removed ${SERVICE_NAME}.service"
