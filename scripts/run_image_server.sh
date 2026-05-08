#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

IMAGE_SERVER_BIN="${IMAGE_SERVER_BIN:-${ROOT_DIR}/build/image_server}"
IMAGE_SERVER_CONFIG="${IMAGE_SERVER_CONFIG:-${ROOT_DIR}/config/image_server.yaml}"
IMAGE_SERVER_PORT="${IMAGE_SERVER_PORT:-}"
DEPTH_HOST="${DEPTH_HOST:-}"
DEPTH_PORT="${DEPTH_PORT:-}"
NO_DEPTH_SERVER="${NO_DEPTH_SERVER:-0}"

if [[ ! -x "${IMAGE_SERVER_BIN}" ]]; then
  echo "image_server not found: ${IMAGE_SERVER_BIN}" >&2
  echo "Run: cmake --build build -j\$(nproc)" >&2
  exit 1
fi

if [[ ! -f "${IMAGE_SERVER_CONFIG}" ]]; then
  echo "image_server config not found: ${IMAGE_SERVER_CONFIG}" >&2
  exit 1
fi

echo "image server config:"
echo "  ROOT_DIR=${ROOT_DIR}"
echo "  IMAGE_SERVER_BIN=${IMAGE_SERVER_BIN}"
echo "  IMAGE_SERVER_CONFIG=${IMAGE_SERVER_CONFIG}"
echo "  IMAGE_SERVER_PORT=${IMAGE_SERVER_PORT:-<from YAML>}"
echo "  DEPTH_HOST=${DEPTH_HOST:-<from YAML>}"
echo "  DEPTH_PORT=${DEPTH_PORT:-<from YAML>}"
echo "  NO_DEPTH_SERVER=${NO_DEPTH_SERVER}"

args=("${IMAGE_SERVER_BIN}" --config "${IMAGE_SERVER_CONFIG}")

[[ -n "${IMAGE_SERVER_PORT}" ]] && args+=(--port "${IMAGE_SERVER_PORT}")
[[ -n "${DEPTH_HOST}" ]] && args+=(--depth-host "${DEPTH_HOST}")
[[ -n "${DEPTH_PORT}" ]] && args+=(--depth-port "${DEPTH_PORT}")

if [[ "${NO_DEPTH_SERVER}" == "1" ]]; then
  args+=(--no-depth-server)
fi

exec "${args[@]}"
