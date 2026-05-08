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
DEPTH_RESPONSE_CODEC="${DEPTH_RESPONSE_CODEC:-jpg}"
DEPTH_JPEG_QUALITY="${DEPTH_JPEG_QUALITY:-90}"

if [[ ! -d "${DEPTH_REPO}" ]]; then
  echo "Depth Anything V2 repo not found: ${DEPTH_REPO}" >&2
  exit 1
fi

if [[ ! -f "${DEPTH_CHECKPOINT}" ]]; then
  echo "Depth Anything V2 checkpoint not found: ${DEPTH_CHECKPOINT}" >&2
  exit 1
fi

echo "depth server config:"
echo "  ROOT_DIR=${ROOT_DIR}"
echo "  DEPTH_REPO=${DEPTH_REPO}"
echo "  DEPTH_CHECKPOINT=${DEPTH_CHECKPOINT}"
echo "  DEPTH_ENCODER=${DEPTH_ENCODER}"
echo "  DEPTH_INPUT_SIZE=${DEPTH_INPUT_SIZE}"
echo "  DEPTH_DEVICE=${DEPTH_DEVICE}"
echo "  DEPTH_PRECISION=${DEPTH_PRECISION}"
echo "  DEPTH_WARMUP=${DEPTH_WARMUP}"
echo "  DEPTH_HOST=${DEPTH_HOST}"
echo "  DEPTH_PORT=${DEPTH_PORT}"
echo "  DEPTH_RESPONSE_CODEC=${DEPTH_RESPONSE_CODEC}"
echo "  DEPTH_JPEG_QUALITY=${DEPTH_JPEG_QUALITY}"

exec "${PYTHON_BIN}" "${ROOT_DIR}/scripts/depth_server.py" \
  --host "${DEPTH_HOST}" \
  --port "${DEPTH_PORT}" \
  --repo_path "${DEPTH_REPO}" \
  --checkpoint "${DEPTH_CHECKPOINT}" \
  --encoder "${DEPTH_ENCODER}" \
  --input_size "${DEPTH_INPUT_SIZE}" \
  --device "${DEPTH_DEVICE}" \
  --precision "${DEPTH_PRECISION}" \
  --warmup "${DEPTH_WARMUP}" \
  --response_codec "${DEPTH_RESPONSE_CODEC}" \
  --jpeg_quality "${DEPTH_JPEG_QUALITY}"
