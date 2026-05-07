#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

PYTHON_BIN="${PYTHON_BIN:-python3}"
DEPTH_REPO="${DEPTH_REPO:-${ROOT_DIR}/Depth-Anything-V2-main}"
DEPTH_CHECKPOINT="${DEPTH_CHECKPOINT:-${DEPTH_REPO}/checkpoints/depth_anything_v2_vits.pth}"
DEPTH_ENCODER="${DEPTH_ENCODER:-vits}"
DEPTH_INPUT_SIZE="${DEPTH_INPUT_SIZE:-280}"
DEPTH_DEVICE="${DEPTH_DEVICE:-cuda}"
DEPTH_PRECISION="${DEPTH_PRECISION:-auto}"
DEPTH_WARMUP="${DEPTH_WARMUP:-2}"
DEPTH_HOST="${DEPTH_HOST:-127.0.0.1}"
DEPTH_PORT="${DEPTH_PORT:-18080}"

exec "${PYTHON_BIN}" "${ROOT_DIR}/scripts/depth_server.py" \
  --host "${DEPTH_HOST}" \
  --port "${DEPTH_PORT}" \
  --repo_path "${DEPTH_REPO}" \
  --checkpoint "${DEPTH_CHECKPOINT}" \
  --encoder "${DEPTH_ENCODER}" \
  --input_size "${DEPTH_INPUT_SIZE}" \
  --device "${DEPTH_DEVICE}" \
  --precision "${DEPTH_PRECISION}" \
  --warmup "${DEPTH_WARMUP}"
