#!/usr/bin/env bash
set -euo pipefail

SERVICE_NAME="minilisysm"
CPU_SET="2"
CONFIG_PATH=""
SERVICE_USER="${USER:-cat}"
SERVICE_GROUP=""

usage() {
    cat <<'EOF'
Usage: scripts/install_service.sh [options]

Installs and starts a systemd service that runs scripts/run.sh in the background at boot.

Options:
  --name NAME        Service name. Default: minilisysm
  --cpu CPUSET      CPU affinity passed to scripts/run.sh. Default: 2
  --config PATH     Optional config file passed to scripts/run.sh.
  --user USER       Linux user used by the service. Default: current user.
  --group GROUP     Linux group used by the service. Default: primary group of --user.
  -h, --help        Show this help.

Examples:
  ./scripts/install_service.sh
  ./scripts/install_service.sh --cpu 1
  ./scripts/install_service.sh --config ./install/etc/minilisysm/lisysm_monitor.ini
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
        --cpu)
            if [[ $# -lt 2 ]]; then
                echo "--cpu requires a CPU set, for example 2 or 0-3." >&2
                exit 2
            fi
            CPU_SET="$2"
            shift 2
            ;;
        --config)
            if [[ $# -lt 2 ]]; then
                echo "--config requires a path." >&2
                exit 2
            fi
            CONFIG_PATH="$2"
            shift 2
            ;;
        --user)
            if [[ $# -lt 2 ]]; then
                echo "--user requires a value." >&2
                exit 2
            fi
            SERVICE_USER="$2"
            shift 2
            ;;
        --group)
            if [[ $# -lt 2 ]]; then
                echo "--group requires a value." >&2
                exit 2
            fi
            SERVICE_GROUP="$2"
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

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${ROOT_DIR}"

if [[ ! -x "${ROOT_DIR}/scripts/run.sh" ]]; then
    echo "run script not found or not executable: ${ROOT_DIR}/scripts/run.sh" >&2
    exit 1
fi
if [[ ! -x "${ROOT_DIR}/install/bin/minilisysm" ]]; then
    echo "installed binary not found: ${ROOT_DIR}/install/bin/minilisysm" >&2
    echo "Run ./scripts/build.sh first." >&2
    exit 1
fi
if ! id "${SERVICE_USER}" >/dev/null 2>&1; then
    echo "service user does not exist: ${SERVICE_USER}" >&2
    exit 1
fi
if [[ -z "${SERVICE_GROUP}" ]]; then
    SERVICE_GROUP="$(id -gn "${SERVICE_USER}")"
fi
if ! getent group "${SERVICE_GROUP}" >/dev/null 2>&1; then
    echo "service group does not exist: ${SERVICE_GROUP}" >&2
    exit 1
fi

CONFIG_ARG=""
if [[ -n "${CONFIG_PATH}" ]]; then
    if [[ ! -f "${CONFIG_PATH}" ]]; then
        echo "config file not found: ${CONFIG_PATH}" >&2
        exit 1
    fi
    CONFIG_ABS="$(realpath "${CONFIG_PATH}")"
    CONFIG_ARG=" --config ${CONFIG_ABS}"
fi

SERVICE_FILE="/etc/systemd/system/${SERVICE_NAME}.service"
TEMPLATE="${ROOT_DIR}/deploy/systemd/minilisysm.service.in"
TMP_FILE="$(mktemp)"
trap 'rm -f "${TMP_FILE}"' EXIT

sed \
    -e "s#@ROOT_DIR@#${ROOT_DIR}#g" \
    -e "s#@SERVICE_USER@#${SERVICE_USER}#g" \
    -e "s#@SERVICE_GROUP@#${SERVICE_GROUP}#g" \
    -e "s#@CPU_SET@#${CPU_SET}#g" \
    -e "s#@CONFIG_ARG@#${CONFIG_ARG}#g" \
    "${TEMPLATE}" > "${TMP_FILE}"

SUDO=()
if [[ "${EUID}" -ne 0 ]]; then
    if ! command -v sudo >/dev/null 2>&1; then
        echo "sudo is required when not running as root." >&2
        exit 1
    fi
    SUDO=(sudo)
fi

"${SUDO[@]}" install -m 0644 "${TMP_FILE}" "${SERVICE_FILE}"
"${SUDO[@]}" systemctl daemon-reload
"${SUDO[@]}" systemctl enable --now "${SERVICE_NAME}.service"

echo "Installed and started ${SERVICE_NAME}.service"
echo "Status:  systemctl status ${SERVICE_NAME}.service"
echo "Logs:    journalctl -u ${SERVICE_NAME}.service -f"
echo "Stop:    sudo systemctl stop ${SERVICE_NAME}.service"
echo "Disable: sudo systemctl disable --now ${SERVICE_NAME}.service"
