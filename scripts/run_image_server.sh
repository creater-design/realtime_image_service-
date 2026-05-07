#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

IMAGE_SERVER_BIN="${IMAGE_SERVER_BIN:-${ROOT_DIR}/build/image_server}"
IMAGE_SERVER_PORT="${IMAGE_SERVER_PORT:-9999}"
DEPTH_HOST="${DEPTH_HOST:-127.0.0.1}"
DEPTH_PORT="${DEPTH_PORT:-18080}"
NO_DEPTH_SERVER="${NO_DEPTH_SERVER:-0}"

if [[ ! -x "${IMAGE_SERVER_BIN}" ]]; then
  echo "image_server not found: ${IMAGE_SERVER_BIN}" >&2
  echo "Run: cmake --build build -j\$(nproc)" >&2
  exit 1
fi

args=("${IMAGE_SERVER_BIN}" --port "${IMAGE_SERVER_PORT}")

if [[ "${NO_DEPTH_SERVER}" == "1" ]]; then
  args+=(--no-depth-server)
else
  args+=(--depth-host "${DEPTH_HOST}" --depth-port "${DEPTH_PORT}")
fi

exec "${args[@]}"
